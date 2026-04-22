"""
הדגמה ויזואלית של pipeline זיהוי קשתית – צד לקוח
תמונה מלאה → פנים → עין → קשתית
תוצאות נשמרות בתיקיית output/
"""

import cv2
import os
import sys
import numpy as np

OUTPUT_DIR = "demo_output"
os.makedirs(OUTPUT_DIR, exist_ok=True)

FACE_CASCADE_PATH = "C:/iris_demo/haarcascade_frontalface_default.xml"
EYE_CASCADE_PATH  = "C:/iris_demo/haarcascade_eye.xml"

# תמיכה בסיומת גדולה/קטנה
import glob as _glob
def _find_image(path):
    if os.path.exists(path): return path
    matches = _glob.glob(path[:-4] + ".*")
    return matches[0] if matches else path

face_cascade = cv2.CascadeClassifier(FACE_CASCADE_PATH)
eye_cascade  = cv2.CascadeClassifier(EYE_CASCADE_PATH)


def save(name, image, step_num, description):
    """שומר תמונה עם כותרת מוטבעת – תמונות קטנות מוגדלות"""
    out = image.copy()
    if len(out.shape) == 2:
        out = cv2.cvtColor(out, cv2.COLOR_GRAY2BGR)

    # הגדלת תמונות קטנות כך שתמיד יהיו לפחות 400px ברוחב
    min_width = 400
    if out.shape[1] < min_width:
        scale = min_width / out.shape[1]
        new_w = int(out.shape[1] * scale)
        new_h = int(out.shape[0] * scale)
        out = cv2.resize(out, (new_w, new_h), interpolation=cv2.INTER_NEAREST)

    # תמונות גדולות מדי – נכווץ
    max_width = 900
    if out.shape[1] > max_width:
        scale = max_width / out.shape[1]
        out = cv2.resize(out, (int(out.shape[1]*scale), int(out.shape[0]*scale)))

    # כותרת בתחתית התמונה
    cv2.rectangle(out, (0, out.shape[0]-40), (out.shape[1], out.shape[0]), (0,0,0), -1)
    cv2.putText(out, f"Step {step_num}: {description}",
                (8, out.shape[0]-12), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255,255,255), 1)

    path = os.path.join(OUTPUT_DIR, f"step{step_num}_{name}.jpg")
    cv2.imwrite(path, out)
    print(f"  [שמרתי] {path}")
    return path


def run_pipeline(frame):
    print("\n=== מתחיל pipeline ===\n")

    # ── שלב 0: תמונה מקורית ──────────────────────────────
    save("original", frame, 0, "Original frame from camera")

    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    equalized = cv2.equalizeHist(gray)

    # ── שלב 1: זיהוי פנים ────────────────────────────────
    faces = face_cascade.detectMultiScale(equalized, scaleFactor=1.1,
                                          minNeighbors=4, minSize=(80, 80))
    step1 = frame.copy()
    if len(faces) == 0:
        print("  ❌ לא נמצאו פנים")
        save("no_face", step1, 1, "No face detected")
        return
    
    # בוחרים את הפנים הגדולות ביותר
    faces = sorted(faces, key=lambda r: r[2]*r[3], reverse=True)
    (fx, fy, fw, fh) = faces[0]
    print(f"  ✔ בחרתי פנים: x={fx} y={fy} w={fw} h={fh}")
    cv2.rectangle(step1, (fx, fy), (fx+fw, fy+fh), (0, 255, 0), 2)
    cv2.putText(step1, "Face", (fx, fy-8), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0,255,0), 2)
    save("face_detected", step1, 1, "Face detected (green box)")
    print(f"  ✔ נמצאו פנים: {len(faces)}")

    face_roi_gray  = gray[fy:fy+fh, fx:fx+fw]
    face_roi_color = frame[fy:fy+fh, fx:fx+fw].copy()
    save("face_crop", face_roi_color, 2, "Face ROI (cropped)")

    # ── שלב 2: זיהוי עיניים ──────────────────────────────
    # מחפשים עיניים רק בחצי העליון של הפנים (לא מגיעים לאף/פה)
    upper_h = int(fh * 0.55)
    upper_half_gray  = face_roi_gray[:upper_h, :]
    upper_half_color = face_roi_color[:upper_h, :].copy()

    # גודל מינימום ריאלי לעין – לפחות 12% מרוחב הפנים
    eye_min = max(30, int(fw * 0.12))
    eyes = eye_cascade.detectMultiScale(upper_half_gray, scaleFactor=1.05,
                                        minNeighbors=8,
                                        minSize=(eye_min, eye_min))

    # step3 – מצייר על כל תמונת הפנים (קואורדינטות זהות כי חתכנו רק מלמטה)
    step3 = face_roi_color.copy()
    # קו אופקי – גבול החיפוש
    cv2.line(step3, (0, upper_h), (fw, upper_h), (255, 255, 0), 2)

    # סינון: y בתחום הגיוני + רוחב מינימלי
    valid_eyes = []
    for (ex, ey, ew, eh) in eyes:
        y_pct   = ey / upper_h
        w_pct   = ew / fw
        ratio   = ew / max(eh, 1)
        print(f"    עין: x={ex} y={ey} w={ew} h={eh} | y%={y_pct:.2f} w%={w_pct:.2f} ratio={ratio:.2f}")
        if 0.05 < y_pct < 0.90 and w_pct > 0.08 and ratio > 0.7:
            valid_eyes.append((ex, ey, ew, eh))
            cv2.rectangle(step3, (ex, ey), (ex+ew, ey+eh), (0, 255, 0), 3)
        else:
            cv2.rectangle(step3, (ex, ey), (ex+ew, ey+eh), (0, 0, 255), 1)

    save("eyes_detected", step3, 3, f"Green=valid eyes, Red=filtered out ({len(valid_eyes)} valid)")
    print(f"  ✔ נמצאו עיניים סה\"כ: {len(eyes)}, תקינות: {len(valid_eyes)}")

    if len(valid_eyes) == 0:
        print("  ❌ לא נמצאו עיניים תקינות")
        return

    # בוחרים את העין הגדולה ביותר מבין התקינות
    (ex, ey, ew, eh) = sorted(valid_eyes, key=lambda e: e[2]*e[3], reverse=True)[0]
    eye_roi_gray  = upper_half_gray[ey:ey+eh, ex:ex+ew]
    eye_roi_color = upper_half_color[ey:ey+eh, ex:ex+ew].copy()
    save("eye_crop", eye_roi_color, 4, "Eye ROI (cropped)")

    # ── שלב 3: זיהוי קשתית ───────────────────────────────
    blurred = cv2.GaussianBlur(eye_roi_gray, (7, 7), 1.5)
    rMin = max(5, ew // 6)
    rMax = ew // 2

    circles = cv2.HoughCircles(blurred, cv2.HOUGH_GRADIENT,
                                dp=1, minDist=max(5, eh//4),
                                param1=50, param2=8,
                                minRadius=rMin, maxRadius=rMax)

    step5 = cv2.cvtColor(eye_roi_gray, cv2.COLOR_GRAY2BGR)

    if circles is None:
        print("  ❌ לא נמצאה קשתית")
        save("no_iris", step5, 5, "Iris not detected")
        return

    circles = np.round(circles[0, :]).astype(int)

    # מעגל קשתית – כתום
    cx, cy, r = circles[0]
    cv2.circle(step5, (cx, cy), r,  (0, 165, 255), 2)
    cv2.circle(step5, (cx, cy), 2,  (0, 165, 255), -1)

    # חיפוש אישון – האישון הוא 25%–55% מרדיוס הקשתית
    pupil_r_max = max(5, int(r * 0.55))
    pupil_r_min = max(3, int(r * 0.25))
    pupils = cv2.HoughCircles(blurred, cv2.HOUGH_GRADIENT,
                               dp=1, minDist=eh//4,
                               param1=80, param2=12,
                               minRadius=pupil_r_min, maxRadius=pupil_r_max)
    if pupils is not None:
        # בוחרים רק אישון שמרכזו קרוב למרכז הקשתית (קונצנטרי)
        best_pupil = None
        for p in np.round(pupils[0]).astype(int):
            px, py, pr = p
            dist = np.sqrt((px - cx)**2 + (py - cy)**2)
            if dist < r * 0.45:   # מרכז האישון לא יכול להיות רחוק מהקשתית
                best_pupil = (px, py, pr)
                break
        if best_pupil:
            px, py, pr = best_pupil
            cv2.circle(step5, (px, py), pr, (0, 0, 255), 2)
            cv2.putText(step5, "Pupil", (px-15, py-pr-4),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.35, (0,0,255), 1)

    cv2.putText(step5, "Iris", (cx-15, cy-r-4),
                cv2.FONT_HERSHEY_SIMPLEX, 0.35, (0,165,255), 1)
    save("iris_detected", step5, 5, "Iris=orange, Pupil=red")
    print(f"  ✔ נמצאה קשתית: מרכז=({cx},{cy}) רדיוס={r}")

    # ── שלב 4: חיתוך הקשתית בלבד ────────────────────────
    ix1 = max(0, cx - r)
    iy1 = max(0, cy - r)
    ix2 = min(eye_roi_gray.shape[1], cx + r)
    iy2 = min(eye_roi_gray.shape[0], cy + r)

    iris_crop = eye_roi_gray[iy1:iy2, ix1:ix2]
    save("iris_crop", iris_crop, 6, "Iris ONLY – this is sent to server")
    print(f"  ✔ גודל קשתית שנחתכה: {iris_crop.shape[1]}x{iris_crop.shape[0]} px")

    # ── סיכום: תמונת pipeline מלאה ───────────────────────
    print(f"\n=== הסתיים! כל התמונות נשמרו בתיקייה: {OUTPUT_DIR}/ ===")


# ── הרצה ────────────────────────────────────────────────
if __name__ == "__main__":
    # אם יש ארגומנט – טוען תמונה מקובץ
    if len(sys.argv) > 1:
        img_path = sys.argv[1]
        frame = cv2.imread(img_path)
        if frame is None:
            print(f"שגיאה: לא ניתן לטעון את הקובץ {img_path}")
            sys.exit(1)
        print(f"טוען תמונה: {img_path}")
    else:
        # מצלם מהמצלמה
        print("פותח מצלמה... (לחץ SPACE לצלם, ESC לביטול)")
        cap = cv2.VideoCapture(0)
        if not cap.isOpened():
            print("שגיאה: לא נמצאה מצלמה. הרץ: python demo.py <נתיב_תמונה>")
            sys.exit(1)

        frame = None
        while True:
            ret, f = cap.read()
            if not ret:
                break
            cv2.imshow("מצלם... לחץ SPACE לצלם", f)
            key = cv2.waitKey(1)
            if key == 32:   # SPACE
                frame = f.copy()
                break
            elif key == 27: # ESC
                break

        cap.release()
        cv2.destroyAllWindows()

        if frame is None:
            print("לא צולמה תמונה.")
            sys.exit(0)

    run_pipeline(frame)
