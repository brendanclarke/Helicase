import json

log_file = "/Users/bc/.gemini/antigravity/brain/3a557c3e-682a-41ce-9715-aa2af6d70bf7/.system_generated/logs/transcript_full.jsonl"
target_file = "/Users/bc/Helicase/Core/Hardware/SD/asyncfatfs/asyncfatfs.c"

with open(log_file, 'r') as f:
    lines = f.readlines()

with open(target_file, 'r') as f:
    content = f.read()

# Collect all edits in order
edits = []
for line in lines:
    try:
        data = json.loads(line)
    except:
        continue
    if 'tool_calls' in data:
        for call in data['tool_calls']:
            name = call.get('name')
            if name in ['multi_replace_file_content', 'replace_file_content']:
                args = call.get('args', {})
                if 'asyncfatfs.c' in args.get('TargetFile', ''):
                    chunks = []
                    if name == 'replace_file_content':
                        chunks = [args]
                    else:
                        c = args.get('ReplacementChunks', [])
                        if isinstance(c, str):
                            c = json.loads(c)
                        chunks = c
                    
                    for chunk in chunks:
                        t = chunk.get('TargetContent')
                        r = chunk.get('ReplacementContent')
                        if t and r:
                            edits.append((args.get('Instruction', ''), t, r))

# Apply edits
applied = 0
for instruction, target, replacement in edits:
    # Only apply if it exists exactly once (or more, but using count=1 limits the explosion, except if multiple occur we want to replace all intended. Actually let's just use string.replace with count=1 if AllowMultiple=False, but since we don't have that info easily, let's just count occurrences.)
    if target in content:
        # Check if replacement is ALREADY in content to prevent double-applying
        if replacement in content and target not in replacement:
            continue
            
        content = content.replace(target, replacement, 1)
        applied += 1
        print(f"Applied: {instruction}")
    else:
        print(f"Skipped (target not found): {instruction}")

with open(target_file, 'w') as f:
    f.write(content)

print(f"\nTotal applied: {applied}")
