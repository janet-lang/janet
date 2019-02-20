# Copyright (C) Calvin Rose 2019

# The bootstrap script is used to produce the source file for
# embedding the core image.

# Tool to dump a marshalled version of the janet core to stdout. The
# image should eventually allow janet to be started from a pre-compiled
# image rather than recompiled every time from the embedded source. More
# work will go into shrinking the image (it isn't currently that large but
# could be smaller), creating the mechanism to load the image, and modifying
# the build process to compile janet with a built image rather than
# embedded source.

# Get image. This image contains as much of the core library and documentation that
# can be written to an image (no cfunctions, no abstracts (stdout, stdin, stderr)),
# everything else goes. Cfunctions and abstracts will be referenced from a registry
# table which will be generated on janet startup.
(do
  (def image (let [env-pairs (pairs (env-lookup *env*))
                   essential-pairs (filter (fn [[k v]] (or (cfunction? v) (abstract? v))) env-pairs)
                   lookup (table ;(mapcat identity essential-pairs))
                   reverse-lookup (invert lookup)]
               (marshal *env* reverse-lookup)))

  # Create C source file that contains images a uint8_t buffer. This
  # can be compiled and linked statically into the main janet library
  # and example client.
  (def chunks (string/bytes image))
  (def image-file (file/open "build/core_image.c" :w))
  (file/write image-file
              "#ifndef JANET_AMALG\n"
              "#include <janet.h>\n"
              "#endif\n"
              "static const unsigned char janet_core_image_bytes[] = {\n")
  (loop [line :in (partition 10 chunks)]
    (def str (string ;(interpose ", " (map (partial string/format "0x%.2X") line))))
    (file/write image-file "    " str ",\n"))
  (file/write image-file
              "    0\n};\n\n"
              "const unsigned char *janet_core_image = janet_core_image_bytes;\n"
              "size_t janet_core_image_size = sizeof(janet_core_image_bytes);\n")
  (file/close image-file))
