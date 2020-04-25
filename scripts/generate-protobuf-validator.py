#!/usr/bin/env python3

import sys
import os

currentDir = os.path.dirname(__file__)
if not currentDir:
    # currentDir is empty - Use local dir
    currentDir = "."

# Make sure that the ProtocolBufferValidation directory is in the path
# of this script. Otherwise the import won't work
sys.path.insert(0, currentDir + os.path.sep + "ProtocolBufferValidation")

from ProtoFile import ProtoFile
from Generator import Generator


def printHelp():
    print("Usage:")
    print(__file__ + " <pathToProtoFile>")
    print()


if __name__ == "__main__":
    if not len(sys.argv) == 2:
        print("[ERROR]: Invalid argument count (%d)" % (len(sys.argv) - 1))
        printHelp()
        sys.exit(1)

    if sys.argv[1].lower() == "-h" or sys.argv[1].lower() == "--help":
        printHelp()
        sys.exit(0)
    
    fileName = sys.argv[1]

    protoFile = ProtoFile.fromFile(fileName)

    if protoFile is None:
        print("[ERROR]: Failed at parsing ProtoFile!")
        sys.exit(1)

    print(protoFile)

    gen = Generator.select("cpp")
    print(gen.__class__)
    gen.generateValidator(protoFile)

