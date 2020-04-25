class Generator:
    """Class interface for the generators that actually generate
    the validation code for a particular programming language"""

    def generateValidator(self, protoFile):
        raise RuntimeError("Not implemented - you have to call this function on a sub-class")


    @staticmethod
    def select(programmingLanguage):
        programmingLanguage = programmingLanguage.lower()

        if programmingLanguage == "cpp" or programmingLanguage == "c++":
            return CppGenerator()
        else:
            raise RuntimeError("No generator known for language \"%s\"" % programmingLanguage)

# We have to define the Generator class before we import the CppGenerator in order to
# avoid circular dependencies.
from CppGenerator import CppGenerator
