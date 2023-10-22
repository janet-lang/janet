### typedef struct {float x; float y; float z;} Vec3;
###
### Vec3 addv(Vec3 a, Vec3 b) {
###     Vec3 ret;
###     ret.x = a.x + b.x;
###     ret.y = a.y + b.y;
###     ret.z = a.z + b.z;
###     return ret;
### }

(def ir-asm
  '((link-name "addv_with_err")
    (parameter-count 2)
    # Types
    (type-prim Real f32)
    (type-struct Vec3 Real Real Real)
    (type-pointer PReal Real)

    # Declarations
    (bind position Vec3)
    (bind velocity Vec3)
    (bind next-position Vec3)
    (bind dest Real)
    (bind lhs Real)
    (bind rhs Real)
    (bind pdest PReal)
    (bind plhs PReal)
    (bind prhs PReal)

    # Code (has type errors)
    (fgetp pdest next-position 0)
    (fgetp plhs position 0)
    (fgetp prhs velocity 0)
    (add dest plhs prhs)
    (store pdest dest)

    (fgetp pdest next-position 1)
    (fgetp plhs position 1)
    (fgetp prhs velocity 1)
    (add dest lhs rhs)
    (load lhs plhs)
    (load rhs prhs)
    (store pdest dest)

    (fgetp pdest next-position 2)
    (fgetp plhs position 2)
    (fgetp prhs velocity 2)
    (add dest plhs prhs)
    (store pdest dest)

    (return next-position)))

(def ctx (sysir/context))
(sysir/asm ctx ir-asm)
(print (sysir/to-c ctx))
