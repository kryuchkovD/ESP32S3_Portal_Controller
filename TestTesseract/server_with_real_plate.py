# server_with_real_plate.py
import os
import time
import re
from difflib import get_close_matches
from flask import Flask, request, jsonify
import cv2
import pytesseract
from PIL import Image
import numpy as np

# ==============================
# Настройки проекта
# ==============================
UPLOAD_FOLDER = "uploads"
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

# FSM STATE
portal_open = False
last_photo_number = ""  # последний распознанный номер для логов

# Разрешённые номера
ALLOWED_NUMBERS = ["М222ММ136", "А123ВС77", "К456ЕК99", "Р789ТУ66", "С321АД50", "Т224ЕМ71"]

# ==============================
# Параметры Tesseract
# ==============================
os.environ['TESSDATA_PREFIX'] = '/usr/share/tesseract-ocr/4.00/tessdata/'
RUS_LETTERS = "ABEKMHOPCTYX"
OCR_WHITELIST = RUS_LETTERS + "0123456789"
RUS_PLATE_REGEX = r"[ABEKMHOPCTYX]\d{3}[ABEKMHOPCTYX]{2}\d{2,3}"

# ==============================
# Haar Cascade для российских номеров
# ==============================
CASCADE_PATH = "haarcascade_russian_plate_number.xml"  # помести xml в папку проекта
if not os.path.exists(CASCADE_PATH):
    raise FileNotFoundError(f"❌ Каскад не найден: {CASCADE_PATH}")

plate_cascade = cv2.CascadeClassifier(CASCADE_PATH)
if plate_cascade.empty():
    raise RuntimeError("❌ Каскад не загрузился")
print("✅ Каскад загружен")

# ==============================
# Вспомогательные функции
# ==============================
def fix_plate_text(text: str) -> str:
    replaces = {"O": "0", "Q": "0", "I": "1", "Z": "2", "B": "8"}
    for k, v in replaces.items():
        text = text.replace(k, v)
    return text

def is_rus_plate(text: str) -> bool:
    return re.fullmatch(RUS_PLATE_REGEX, text) is not None

def ocr_real_plate(image_path: str, debug=False):
    """Распознавание номеров на реальном изображении"""
    frame = cv2.imread(image_path)
    if frame is None:
        return []

    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    plates = plate_cascade.detectMultiScale(
        gray, scaleFactor=1.05, minNeighbors=6, minSize=(80, 25)
    )

    results = []
    for (x, y, w, h) in plates:
        pad = 15
        x1 = max(0, x - pad)
        y1 = max(0, y - pad)
        x2 = min(gray.shape[1], x + w + pad)
        y2 = min(gray.shape[0], y + h + pad)

        plate_img = gray[y1:y2, x1:x2]
        plate_img = cv2.resize(plate_img, None, fx=3, fy=3, interpolation=cv2.INTER_CUBIC)
        clahe = cv2.createCLAHE(2.0, (8,8))
        plate_img = clahe.apply(plate_img)
        plate_img = cv2.GaussianBlur(plate_img, (3,3), 0)

        if debug:
            cv2.imshow("Plate", plate_img)
            cv2.waitKey(0)

        pil_img = Image.fromarray(plate_img)
        text = pytesseract.image_to_string(
            pil_img,
            lang="eng",
            config=f"--oem 3 --psm 8 -c tessedit_char_whitelist={OCR_WHITELIST}"
        )
        text = re.sub(r"\s+", "", text)
        text = fix_plate_text(text)
        if text:
            results.append(text)
    return results

def ocr_process(image_path):
    """Фильтрация по российскому номеру"""
    return [t for t in ocr_real_plate(image_path, debug=False) if is_rus_plate(t)]

def match_allowed(recognized_list):
    """Сравнение распознанных номеров с разрешёнными"""
    for candidate in recognized_list:
        if candidate in ALLOWED_NUMBERS:
            return candidate, True
    best_match = ""
    for candidate in recognized_list:
        close = get_close_matches(candidate, ALLOWED_NUMBERS, n=1, cutoff=0)
        if close:
            best_match = close[0]
            break
    return best_match, best_match != ""

# ==============================
# Flask сервер
# ==============================
app = Flask("esp32_server")

@app.route("/ping", methods=["GET"])
def ping():
    print("PING FROM ESP32")
    return jsonify(ok=True, message="pong")

@app.route("/check", methods=["POST"])
def check():
    global portal_open, last_photo_number
    print("\n=== /check CALLED ===")

    if not request.data:
        return jsonify(ok=False, error="empty body"), 400

    content_type = request.headers.get("Content-Type", "")
    print(f"[INFO] Content-Type: {content_type}")

    # ===== TEXT =====
    if content_type.startswith("text/plain"):
        text = request.data.decode("utf-8", errors="replace")
        print(f"[TEXT] {text}")

        if text.strip() == "Прием. Холл сработал!":
            print("[INFO] Холл сработал, ждем фото для проверки номера")

        return jsonify(ok=True, type="text")

    # ===== JPEG =====
    if content_type.startswith("image/jpeg"):
        filename = f"{int(time.time())}_photo.jpg"
        filepath = os.path.join(UPLOAD_FOLDER, filename)

        with open(filepath, "wb") as f:
            f.write(request.data)

        print(f"[PHOTO] Saved: {filename}")

        recognized = ocr_process(filepath)
        print(f"[OCR] Recognized candidates: {recognized}")

        number, allowed = match_allowed(recognized)
        print(f"[MATCH] Number: {number}, Allowed: {allowed}")

        portal_open = allowed
        last_photo_number = number

        return jsonify(ok=allowed, number=number, candidates=recognized, file=filename)

    # ===== UNKNOWN =====
    print("[WARN] Unknown Content-Type, saving raw data")
    filename = f"{int(time.time())}_raw.bin"
    filepath = os.path.join(UPLOAD_FOLDER, filename)
    with open(filepath, "wb") as f:
        f.write(request.data)
    return jsonify(ok=True, type="unknown", file=filename)

@app.route("/check/result", methods=["GET"])
def result():
    global portal_open, last_photo_number
    if portal_open:
        portal_open = False  # одноразовый доступ
        print(f"[FSM] Result = true (номер: {last_photo_number})")
        return "true"
    print("[FSM] Result = false")
    return "false"

# ==============================
# MAIN
# ==============================
if __name__ == "__main__":
    print("Server starting on 0.0.0.0:5000 ...")
    app.run(host="0.0.0.0", port=5000)
