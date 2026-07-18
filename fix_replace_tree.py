import re

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

# remove duplicate replaceTreeContinue
content = re.sub(r'static void afatfs_replaceTreeContinue\(afatfsFile_t \*file\)\n\{\n    switch \(opState->phase\) \{.*?\n\}\n\n/\*', '/*', content, flags=re.DOTALL)

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)
