Import("env")

env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.elf",
    env.VerboseAction(" ".join([
        "$OBJCOPY",
        "-j", ".text",
        "-j", ".data",
        "-O", "binary",
        "$BUILD_DIR/${PROGNAME}.elf",
        "$BUILD_DIR/${PROGNAME}.bin",
    ]), "Generating $BUILD_DIR/${PROGNAME}.bin")
)
