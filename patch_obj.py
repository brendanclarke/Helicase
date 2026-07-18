import re

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

content = content.replace("op->object.displayName", "op->object.id.displayName")
content = content.replace("op->object.shortName", "op->object.id.shortName")
content = content.replace("op->object.sfnEntry", "op->object.id.sfnEntry")
content = content.replace("op->object.lfnFirstEntry", "op->object.id.lfnFirstEntry")
content = content.replace("op->object.lfnEntryCount", "op->object.id.lfnEntryCount")
content = content.replace("op->object.kind", "op->object.id.kind")
content = content.replace("op->object.attrib", "op->object.id.attrib")
content = content.replace("op->object.firstCluster", "op->object.id.firstCluster")
content = content.replace("op->object.logicalSize", "op->object.id.logicalSize")

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)
