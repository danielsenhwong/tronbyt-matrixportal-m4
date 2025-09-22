import os
import tempfile
from flask import Flask, send_file, abort
from PIL import Image, features, ImageDraw, ImageSequence
import imageio.v3 as iio
import requests
from io import BytesIO
import subprocess
import traceback

app = Flask(__name__)

PIXLET_URL = 'http://10.50.15.20:8000/66b20de2/next'
TARGET_SIZE = (64, 32)

@app.route('/feed', methods=['GET'])
def convert_webp():
    try:
        # Fetch the WebP image from Pixlet
        response = requests.get(PIXLET_URL)
        response.raise_for_status()
        image_bytes = response.content

        # Use Pillow to extract all frames and durations robustly
        with Image.open(BytesIO(image_bytes)) as im:
            is_animated = getattr(im, "is_animated", False)
            pil_frames = []
            durations = []
            prev = Image.new("RGBA", im.size, (0, 0, 0, 0))
            for frame in ImageSequence.Iterator(im):
                frame_rgba = frame.convert("RGBA")
                duration = frame.info.get("duration", im.info.get("duration", 350))

                # Composite this frame over previous accumulated image
                coalesced = prev.copy()
                coalesced.alpha_composite(frame_rgba)

                # Resize to target and convert to RGB for GIF
                resized = coalesced.resize(TARGET_SIZE, Image.LANCZOS)
                pil_frames.append(resized.convert("RGB"))
                durations.append(duration)

                prev = coalesced  # Next frame composites over this

#                # Resize and paste to black canvas
#                frame = frame.convert("RGB").resize(TARGET_SIZE, Image.LANCZOS)
#                canvas = Image.new("RGB", TARGET_SIZE, (0, 0, 0))
#                canvas.paste(frame, (0, 0))
#                pil_frames.append(canvas)
#                # Duration in milliseconds (default to 350ms if absent)
#                durations.append(frame.info.get("duration", im.info.get("duration", 350)))

            if len(pil_frames) > 1:
                with tempfile.NamedTemporaryFile(suffix=".gif", delete=False) as tmp_in:
                    pil_frames[0].save(
                        tmp_in.name,
                        format="GIF",
                        save_all=True,
                        append_images=pil_frames[1:],
                        loop=0,
                        duration=durations,
                        disposal=2,
                        optimize=False
                    )
                    tmp_in_path = tmp_in.name

        # Read frames from WebP
#        frames = list(iio.imiter(image_bytes, extension=".webp"))
##        metas = list(iio.immeta(image_bytes, extension=".webp"))
#        is_animated = len(frames) > 1

#        if is_animated:
#            pil_frames = []
#            durations = []
#            for frame in frames:
#                # Get meta
#                meta = getattr(frame, "meta", {})

#                # Always convert to full canvas
#                pil_frame = Image.fromarray(frame).convert("RGB").resize(TARGET_SIZE, Image.LANCZOS)
#                canvas = Image.new("RGB", TARGET_SIZE, (0, 0, 0))
#                canvas.paste(pil_frame, (0, 0))
#                pil_frames.append(canvas)

#                # Meta duration is in seconds; convert to miliseconds for GIF
#                duration_ms = int(meta.get("duration", 0.35) * 1000) # fallback 350ms
#                durations.append(duration_ms)

#            with tempfile.NamedTemporaryFile(suffix=".gif", delete=False) as tmp_in:
#                pil_frames[0].save("/app/data/test_frame.png")
#                pil_frames[0].save(
#                    tmp_in.name,
#                    format="GIF",
#                    save_all=True,
#                    append_images=pil_frames[1:],
#                    loop=1,
#                    duration=durations,
#                    disposal=2,
#                    optimize=False
#                )
#                tmp_in_path = tmp_in.name

                # Prepare output temp file
                with tempfile.NamedTemporaryFile(suffix=".gif", delete=False) as tmp_out:
                    tmp_out_path = tmp_out.name

                # Run ImageMagick to force full frames
                subprocess.run([
                    "convert", tmp_in_path, "-coalesce", tmp_out_path
                ], check=True)

                # Read processed GIF
                with open(tmp_out_path, "rb") as f:
                    gif_bytes = BytesIO(f.read())

                # Clean up temp files
                os.remove(tmp_in_path)
                os.remove(tmp_out_path)

                gif_bytes.seek(0)
                return send_file(gif_bytes, mimetype="image/gif")
            else:
                # Convert to PNG (single frame), check for WebP support
                if not features.check('webp'):
                    return "WebP support not available in Pillow!", 500
                img = Image.open(BytesIO(image_bytes)).convert("RGB")
                img = img.resize(TARGET_SIZE, Image.LANCZOS)
                png_bytes = BytesIO()
                img.save(png_bytes, format="PNG")
                png_bytes.seek(0)
                return send_file(png_bytes, mimetype="image/png")
    except Exception as e:
        print("Error in conversion:", e)
        traceback.print_exc()
        abort(500, str(e))

# def serve_png():
#    # Get WebP from tronbyt/pixlet
#    resp = requests.get(PIXLET_URL)
#    img = Image.open(BytesIO(resp.content)).convert("RGB")  # <-- force RGB
#    buf = BytesIO()
#    img.save(buf, format='PNG')
#    buf.seek(0)
#    return send_file(buf, mimetype='image/png')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8001)
