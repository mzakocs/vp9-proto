f = open("four-colors-vp9.webm", "rb")

file_bytes = f.read()

frame = file_bytes[814:1349]  # first frame
# frame = file_bytes[1355:1389]  # second frame

f = open("./test_frame.vp9", "wb")

f.write(frame)
