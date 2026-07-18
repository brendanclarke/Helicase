import json
import re

with open("edits.txt", "r") as f:
    text = f.read()

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

# edits.txt format:
# --- Found edit for asyncfatfs.c ---
# <instruction>
# Replacement Chunks:
# [
#   {
#     "TargetContent": "...",
#     "ReplacementContent": "..."
#   }
# ]
# OR
# Replacement Content:
# ...

# We'll just look for Replacement Chunks as JSON, and Replacement Content as block
blocks = text.split("--- Found edit for asyncfatfs.c ---")
applied = 0
for block in blocks[1:]:
    if "Replacement Chunks:\n[" in block:
        try:
            chunks_str = block[block.find("[\n"):block.rfind("]")+1]
            chunks = json.loads(chunks_str)
            for chunk in chunks:
                target = chunk.get("TargetContent", "")
                repl = chunk.get("ReplacementContent", "")
                if target and repl:
                    if target in content:
                        content = content.replace(target, repl, 1)
                        applied += 1
                    else:
                        print(f"Target not found")
        except Exception as e:
            print("Error parsing chunk", e)
    elif "Replacement Content:\n" in block:
        repl = block.split("Replacement Content:\n")[1].strip()
        # if there's no target, it's hard to apply. But wait, some of these were replace_file_content or what?
        print("Skipping block with only replacement content")

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)

print(f"Applied {applied} edits from edits.txt")
