from flask import Flask, request, jsonify
import os, time, cv2, pytesseract, re
from difflib import get_close_matches

app = Flask("esp32_server")

UPLOAD_FOLDER = "uploads"
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

# ===== FSM STATE =====
portal_open = False
last_photo_number = ""  # последний распознанный номер для логов

# ===== TESSERACT =====
os.environ['TESSDATA_PREFIX'] = '/usr/share/tesseract-ocr/4.00/tessdata/'
ALLOWED_NUMBERS = ["М222ММ136", "А123ВС77", "К456ЕК99", "Р789ТУ66", "С321АД50", "Т224ЕМ71"]

# ==============================
# OCR ФУНКЦИЯ
# ==============================
def ocr_process(image_path):
    img = cv2.imread(image_path, cv2.IMREAD_GRAYSCALE)
    img = cv2.resize(img, None, fx=3, fy=3, interpolation=cv2.INTER_CUBIC)

    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
    img = clahe.apply(img)

    _, img = cv2.threshold(img, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    img = cv2.medianBlur(img, 3)

    custom_config = r'--oem 3 --psm 7 -c tessedit_char_whitelist=АВЕКМНОРСТУХ0123456789'

    try:
        data = pytesseract.image_to_data(img, lang='rus', config=custom_config, output_type=pytesseract.Output.DICT)
    except pytesseract.TesseractError:
        return []

    texts = []
    n_boxes = len(data['level'])
    for i in range(n_boxes):
        t = data['text'][i].strip()
        if t:
            # коррекция ошибок
            t = t.replace('N','М').replace('I','1').replace('O','0')
            t = re.sub(r'[^АВЕКМНОРСТУХ0-9]', '', t)
            if t:
                texts.append(t)

    # Убираем дубли и берем топ-5 вариантов
    unique_texts = list(dict.fromkeys(texts))
    return unique_texts[:5]

# ==============================
# ВЫБОР НАИБОЛЕЕ ПОХОЖЕГО
# ==============================
def match_allowed(recognized_list):
    for candidate in recognized_list:
        if candidate in ALLOWED_NUMBERS:
            return candidate, True
    # Если точного совпадения нет, ищем максимально похожее
    best_match = ""
    for candidate in recognized_list:
        close = get_close_matches(candidate, ALLOWED_NUMBERS, n=1, cutoff=0)
        if close:
            best_match = close[0]
            break
    return best_match, best_match != ""

# ================== PING ==================
@app.route("/ping", methods=["GET"])
def ping():
    print("PING FROM ESP32")
    return jsonify(ok=True, message="pong")

# ================== CHECK ==================
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

        # Текстовый POST больше не открывает ворота
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

        # ===== OCR =====
        recognized = ocr_process(filepath)
        print(f"[OCR] Recognized candidates: {recognized}")

        # ===== СРАВНЕНИЕ С РАЗРЕШЕННЫМИ =====
        number, allowed = match_allowed(recognized)
        print(f"[MATCH] Number: {number}, Allowed: {allowed}")

        # Открытие ворот только если номер разрешён
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

# ================== RESULT ==================
@app.route("/check/result", methods=["GET"])
def result():
    global portal_open, last_photo_number

    if portal_open:
        portal_open = False  # одноразовый доступ
        print(f"[FSM] Result = true (номер: {last_photo_number})")
        return "true"

    print("[FSM] Result = false")
    return "false"

if __name__ == "__main__":
    print("Server starting on 0.0.0.0:5000 ...")
    app.run(host="0.0.0.0", port=5000)
