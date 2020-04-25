from Generator import Generator
from ProtoFile import ProtoFile
from FieldAnnotation import RestrictAnnotation

class CppGenerator(Generator):
    def generateValidator(self, protoFile):
        if not isinstance(protoFile, ProtoFile):
            raise RuntimeError("Expected object of type ProtoFile but got \"%s\"" % protoFile.__class__.__name__)
        
        # TODO: iterate through all messages and generate the code
