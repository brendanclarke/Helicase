import json

log_file = "/Users/bc/.gemini/antigravity/brain/3a557c3e-682a-41ce-9715-aa2af6d70bf7/.system_generated/logs/transcript_full.jsonl"
target_file = "/Users/bc/Helicase/Core/Hardware/SD/asyncfatfs/asyncfatfs.c"

with open(log_file, 'r') as f:
    lines = f.readlines()

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
                    print(f"Applying edit: {args.get('Instruction', '')}")
                    with open(target_file, 'r') as f:
                        content = f.read()
                    
                    chunks = []
                    if name == 'replace_file_content':
                        chunks = [args]
                    else:
                        chunks = args.get('ReplacementChunks', [])
                        if isinstance(chunks, str):
                            chunks = json.loads(chunks)
                    
                    for chunk in chunks:
                        target = chunk.get('TargetContent')
                        replacement = chunk.get('ReplacementContent')
                        if target is not None and replacement is not None:
                            if target in content:
                                content = content.replace(target, replacement)
                                print("  -> Applied successfully")
                            else:
                                print("  -> Failed to find target")
                    
                    with open(target_file, 'w') as f:
                        f.write(content)

