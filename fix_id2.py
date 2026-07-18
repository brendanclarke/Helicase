with open("/Users/bc/Helicase/Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

content = content.replace("object->kind = ", "object->id.kind = ")
content = content.replace("object->attrib = ", "object->id.attrib = ")
content = content.replace("object->logicalSize = ", "object->id.logicalSize = ")
content = content.replace("object->firstCluster = ", "object->id.firstCluster = ")

with open("/Users/bc/Helicase/Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)
