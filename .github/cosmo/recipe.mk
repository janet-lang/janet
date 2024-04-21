# this recipe is copied into superconfigure/janet/BUILD.mk
o/janet/downloaded: \
	DL_COMMAND = cp -r $(SOURCE_DIR) ./ && ls -al && ls -al ..

o/janet/patched: PATCH_COMMAND = $(DUMMYLINK0)

o/janet/configured.x86_64: \
	CONFIG_COMMAND = cp -r $(BASELOC)/o/janet/janet/* ./

o/janet/configured.aarch64: \
	CONFIG_COMMAND = cp -r $(BASELOC)/o/janet/janet/* ./

o/janet/built.x86_64: \
	BUILD_COMMAND = make PREFIX=$(COSMOS) HAS_SHARED=0 JANET_NO_AMALG=1

o/janet/built.aarch64: \
	BUILD_COMMAND = make PREFIX=$(COSMOS) HAS_SHARED=0 JANET_NO_AMALG=1

o/janet/installed.x86_64: \
	INSTALL_COMMAND = cp build/janet $(COSMOS)/bin/

o/janet/installed.aarch64: \
	INSTALL_COMMAND = cp build/janet $(COSMOS)/bin/

o/janet/built.fat: \
	BINS = janet
