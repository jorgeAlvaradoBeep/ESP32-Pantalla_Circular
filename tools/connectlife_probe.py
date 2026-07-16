#!/usr/bin/env python3
"""Replica en la PC el flujo ConnectLife del firmware (ConnectLifeClient.cpp)
para diagnosticar por que el ESP32 no encuentra dispositivos.

Uso:
    tools/.venv/bin/python tools/connectlife_probe.py [--base-url URL]

Credenciales: variables de entorno CONNECTLIFE_LOGIN / CONNECTLIFE_PASSWORD,
o se piden interactivamente (la password no se muestra al teclear).
"""

import argparse
import base64
import getpass
import hashlib
import json
import os
import sys
import time

import requests
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding

# --- Mismas constantes que ConnectLife/Config.h ---------------------------
GIGYA_API_KEY = "4_yhTWQmHFpZkQZDSV1uV-_A"
GIGYA_GMID = (
    "gmid.ver4.AtLt3mZAMA.C8m5VqSTEQDrTRrkYYDgOaJWcyQ-XHow5nzQSXJF3EO3TnqTJ8tKUmQaaQ6z8p0s."
    "zcTbHe6Ax6lHfvTN7JUj7VgO4x8Vl-vk1u0kZcrkKmKWw8K9r0shyut_at5Q0ri6zTewnAv2g1Dc8dauuyd-Sw.sc3"
)
CLIENT_ID = "5065059336212"
CLIENT_SECRET = "07swfKgvJhC3ydOUS9YV_SwVz0i4LKqlOLGNUukYHVMsJRF1b-iWeUGcNlXyYCeK"
REDIRECT_URI = "https://api.connectlife.io/swagger/oauth2-redirect.html"
APP_ID = "47110565134383"
APP_SECRET = "yOzhz6junYno-nmULM3Wr7PU_dpSZN22ZdluvVWZ4uW5ZwwG8fIGCHTbrhcnU-iv"
SIGNATURE_SALT = "D9519A4B756946F081B7BB5B5E8D1197"
DEFAULT_BASE_URL = "https://clife-eu-gateway.hijuconn.com"
PUBLIC_KEY_PEM = b"""-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAyyWrNG6q475HIHu7sMVu
vHof6vlgPeixmxa4EL/UsvVvHPz33NnWoQetQqit9TBNzUjMXw0KlY9PXM4iqHUU
U+dSyNDq1jZWIiJ2C2FccppswJtIKL3NRMFvT9PFh6NlP/4FUcQKojgKFbF7Kacc
JPKYHlwaO7qgoIjLxAHlSOXGpucJcOkPzT2EqsSVnW8sn8kenvNmghXDayhgxsh6
AyxK4kehJplEnmX/iYCfNoFXknGcLqFWYccgBz3fybvx30C/0IgU1980L8QsUAv5
esZmN8ugnbRgLRxKRlkQQLxQAiZMZdKTAx665YflT3YMHJvEFE8c2XFgoxHzSMc4
BwIDAQAB
-----END PUBLIC KEY-----
"""

SESSION = requests.Session()
SESSION.headers["User-Agent"] = "Runner/2.0.6 (iPhone; iOS 17.2.1; Scale/3.00)"


def redact(secret: str) -> str:
    if not secret:
        return "<vacio>"
    if len(secret) <= 12:
        return f"{secret[:3]}... ({len(secret)} chars)"
    return f"{secret[:6]}...{secret[-6:]} ({len(secret)} chars)"


def step(title: str):
    print(f"\n{'=' * 70}\n  {title}\n{'=' * 70}")


def die(message: str, response_json=None):
    print(f"\n*** FALLO: {message}")
    if response_json is not None:
        print(json.dumps(response_json, indent=2, ensure_ascii=False)[:2000])
    sys.exit(1)


def signature(data: dict) -> str:
    # Identico a getSignature() del firmware y del repo de bilan:
    # ordenar claves, unir k=v con '&', agregar salt, SHA-256 binario,
    # cifrar con RSA PKCS#1 v1.5 y codificar en base64.
    parts = []
    for key in sorted(data):
        value = data[key]
        if isinstance(value, (dict, list)):
            value = json.dumps(value, separators=(",", ":"))
        parts.append(f"{key}={value}")
    to_hash = "&".join(parts) + SIGNATURE_SALT
    digest = hashlib.sha256(to_hash.encode()).digest()
    public_key = serialization.load_pem_public_key(PUBLIC_KEY_PEM)
    encrypted = public_key.encrypt(digest, padding.PKCS1v15())
    return base64.b64encode(encrypted).decode()


def common_payload(access_token: str) -> dict:
    ts = str(int(time.time() * 1000))
    return {
        "appId": APP_ID,
        "appSecret": APP_SECRET,
        "languageId": "12",
        "randStr": hashlib.md5(ts.encode()).hexdigest(),
        "timeStamp": ts,
        "timezone": "1.0",
        "version": "5.0",
        "accessToken": access_token,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL,
                        help=f"Gateway ConnectLife (default: {DEFAULT_BASE_URL})")
    args = parser.parse_args()

    login = os.environ.get("CONNECTLIFE_LOGIN") or input("Email ConnectLife: ")
    password = os.environ.get("CONNECTLIFE_PASSWORD") or getpass.getpass("Password ConnectLife: ")

    step("1/5 Gigya accounts.login")
    resp = SESSION.post("https://accounts.eu1.gigya.com/accounts.login", data={
        "loginID": login,
        "password": password,
        "APIKey": GIGYA_API_KEY,
        "gmid": GIGYA_GMID,
    }).json()
    login_token = (resp.get("sessionInfo") or {}).get("cookieValue")
    uid = resp.get("UID")
    if not login_token or not uid:
        die("Gigya no devolvio sesion (¿cuenta creada con Google/Apple? ¿password correcto?)", resp)
    print(f"OK  login_token={redact(login_token)}  UID={redact(uid)}")

    step("2/5 Gigya accounts.getJWT")
    resp = SESSION.post("https://accounts.eu1.gigya.com/accounts.getJWT", data={
        "APIKey": GIGYA_API_KEY,
        "gmid": GIGYA_GMID,
        "login_token": login_token,
    }).json()
    id_token = resp.get("id_token")
    if not id_token:
        die("getJWT no devolvio id_token", resp)
    print(f"OK  id_token={redact(id_token)}")

    step("3/5 oauth.hijuconn.com /oauth/authorize")
    resp = SESSION.post("https://oauth.hijuconn.com/oauth/authorize", json={
        "client_id": CLIENT_ID,
        "idToken": id_token,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "thirdType": "CDC",
        "thirdClientId": uid,
    }).json()
    code = resp.get("code")
    if not code:
        die("authorize no devolvio code", resp)
    print(f"OK  code={redact(code)}")

    step("4/5 oauth.hijuconn.com /oauth/token")
    resp = SESSION.post("https://oauth.hijuconn.com/oauth/token", data={
        "client_id": CLIENT_ID,
        "code": code,
        "grant_type": "authorization_code",
        "client_secret": CLIENT_SECRET,
        "redirect_uri": REDIRECT_URI,
    }).json()
    access_token = resp.get("access_token")
    if not access_token:
        die("token no devolvio access_token", resp)
    print(f"OK  access_token={redact(access_token)}")
    print(f"    refresh_token={redact(resp.get('refresh_token', ''))}  expires_in={resp.get('expires_in')}")

    # Diagnostico del bug de overflow del firmware original (doc de 1536 bytes):
    esp32_estimate = 715 + len(access_token)
    print(f"\n    [ESP32] payload firmado estimado: ~{esp32_estimate} bytes "
          f"({'NO cabia' if esp32_estimate > 1536 else 'cabia'} en el DynamicJsonDocument(1536) original; "
          f"el parche lo subio a 6144)")

    step(f"5/5 GET {args.base_url}/clife-svc/pu/get_device_status_list")
    payload = common_payload(access_token)
    payload["sign"] = signature(payload)
    http = SESSION.get(f"{args.base_url}/clife-svc/pu/get_device_status_list", params=payload)
    print(f"HTTP {http.status_code}, {len(http.content)} bytes")
    try:
        data = http.json()
    except ValueError:
        die(f"Respuesta no es JSON: {http.text[:500]}")

    redacted = json.dumps(data, indent=2, ensure_ascii=False).replace(access_token, "<accessToken>")
    print("\n--- Respuesta completa " + "-" * 46)
    print(redacted)
    print("-" * 70)

    devices = (data.get("response") or {}).get("deviceList")
    if devices is None:
        devices = data.get("deviceList")

    step("Veredicto")
    if devices is None:
        print("El servidor NO devolvio deviceList -> rechazo la peticion (firma/token/timestamp)")
        print("o la estructura cambio. Revisa arriba el codigo/mensaje de error del cuerpo.")
        sys.exit(1)
    if not devices:
        print("deviceList VACIO con login correcto -> tus dispositivos no estan en este gateway.")
        print("Probable cuenta de otra region. Prueba otro gateway con --base-url.")
        sys.exit(1)

    print(f"{len(devices)} dispositivo(s) en la cuenta:\n")
    for d in devices:
        puid = d.get("puid", "?")
        type_code = str(d.get("deviceTypeCode", "?"))
        offline_state = d.get("offlineState", "?")
        name = d.get("deviceNickName", "?")
        matches = offline_state == 1 and type_code in ("009", "006", "008")
        n_status = len(d.get("statusList") or {})
        print(f"  puid={puid}  nombre={name}  type={type_code}  online={offline_state}  "
              f"statusList={n_status} props")
        print(f"    -> filtro estricto del ESP32 (type 009/006/008 + online): "
              f"{'PASA' if matches else 'NO PASA (el parche usa fallback)'}")


if __name__ == "__main__":
    main()
