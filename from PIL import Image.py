import os
import tkinter as tk
from tkinter import filedialog
import importlib

try:
    Image = importlib.import_module("PIL.Image")
except ModuleNotFoundError as exc:
    raise SystemExit(
        "Pillow is required. Install it with: python -m pip install Pillow"
    ) from exc


def select_image_file():
    root = tk.Tk()
    root.withdraw()
    root.attributes("-topmost", True)  # Bring picker window to front

    file_path = filedialog.askopenfilename(
        title="Select Image to Convert for STM32 NTSC",
        filetypes=[
            ("Image Files", "*.jpg *.jpeg *.png *.bmp *.webp *.tiff"),
            ("All Files", "*.*"),
        ],
    )
    root.destroy()
    return file_path


def image_to_c_array(image_path, output_header, width=320, height=200):
    if not image_path or not os.path.exists(image_path):
        print("No valid image selected. Exiting.")
        return

    # Load image and convert to grayscale
    img = Image.open(image_path).convert("L")

    # Resize image to target NTSC resolution
    img = img.resize((width, height), Image.Resampling.LANCZOS)

    # Apply Floyd-Steinberg dithering for 1-bit monochrome output
    img = img.convert("1", dither=Image.Dither.FLOYDSTEINBERG)

    pixels = img.load()
    bytes_per_row = width // 8

    c_code = []
    c_code.append("#include <stdint.h>\n")
    c_code.append(f"// Source Image: {os.path.basename(image_path)}")
    c_code.append(f"// Resolution: {width}x{height}")
    c_code.append(f"// Size: {bytes_per_row * height} bytes\n")
    c_code.append(
        f"const uint8_t image_ntsc[{bytes_per_row * height}] __attribute__((aligned(4))) = {{"
    )

    for y in range(height):
        row_bytes = []
        for x_byte in range(bytes_per_row):
            byte_val = 0
            for bit in range(8):
                x = x_byte * 8 + bit
                # 0 for black, 1 for white (MSB first)
                if pixels[x, y] > 0:
                    byte_val |= 1 << (7 - bit)
            row_bytes.append(f"0x{byte_val:02X}")

        c_code.append("    " + ", ".join(row_bytes) + ",")

    c_code.append("};\n")

    with open(output_header, "w") as f:
        f.write("\n".join(c_code))

    print(f"\nSuccess! Header generated on Desktop: {os.path.abspath(output_header)}")


# Target the Desktop directory explicitly
desktop_dir = os.path.join(os.path.expanduser("~"), "Desktop")
output_file = os.path.join(desktop_dir, "image_data.h")

selected_image = select_image_file()
if selected_image:
    image_to_c_array(selected_image, output_file, width=320, height=200)