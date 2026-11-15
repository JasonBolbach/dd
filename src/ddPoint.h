#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

/**
 * @file ddPoint.h
 * Points, 3x3 matrices and transforms. Graphics-free and float-only.
 *
 * Everything here is plain 2D affine maths in homogeneous coordinates: a point is (x, y, 1) and a transform is a
 * 3x3 matrix, so translation, rotation and scale all compose by matrix multiplication. The 3x3 form is deliberate:
 * it is what a 3D extension would generalise.
 *
 * @note Only `float` is used (never `double`), because most target microcontrollers have a single-precision FPU only.
 * @note Angles are in RADIANS everywhere in dd. Use DDD_DEG2RAD / DDD_RAD2DEG to convert.
 * @par Macros
 * DDD_PI (float pi), DDD_DEG2RAD(a) and DDD_RAD2DEG(a) (both cast the argument to float, so integer degrees are fine).
 */
#define DDD_PI 3.14159265358979323846f
#define DDD_DEG2RAD(A) (((float)(A))*DDD_PI/180.0f)
#define DDD_RAD2DEG(A) (((float)(A))*180.0f/DDD_PI)

struct Matrix3x3;

/**
 * Homogeneous 2D point (x, y, 1).
 *
 * The workhorse value type of the library: 12 bytes, trivially copyable, safe to memcpy. Access the coordinates with
 * `p[0]` (x) and `p[1]` (y); `p[2]` is the homogeneous weight and is 1 for every point you construct.
 *
 * @code
 * ddP a(1, 2), b(4, 6);
 * float d = a.DistanceTo(b);   // 5
 * ddP mid((a[0] + b[0]) / 2, (a[1] + b[1]) / 2);
 * @endcode
 *
 * @warning `p[2]` is the homogeneous weight. Affine transforms keep it at 1 and the 2D code relies on that, so do not
 *          store other data in it.
 * @note Passing `ddP` by value is cheap (12 bytes); the library does so for small helpers and uses `const ddP&` elsewhere.
 */
struct ddP {
    float M[3] = {0.0f, 0.0f, 1.0f};

    /**
     * Default constructor: the point (0, 0), weight 1.
     */
    ddP() = default;
    /**
     * Constructs the point (x, y).
     * @param x  X coordinate (world units, or pixels - the type does not care).
     * @param y  Y coordinate.
     */
    ddP(float x, float y) { Set(x, y); }

    /**
     * Overwrites the point and resets the weight to 1.
     * @param x  New X (default 0).
     * @param y  New Y (default 0).
     */
    void Set(float x = 0.0f, float y = 0.0f) { M[0] = x; M[1] = y; M[2] = 1.0f; }
    /**
     * Sets the point to the origin (0, 0). Same as Set(0, 0).
     * @note Named Zero (not SetIdentity) because a point has no identity, only an origin.
     */
    void Zero() { Set(0.0f, 0.0f); }

    /**
     * Euclidean distance to another point.
     * @param p  The other point.
     * @return   Distance, always >= 0.
     * @note Uses a square root. To only COMPARE distances, compare squared distances yourself and save the sqrtf.
     */
    float DistanceTo(const ddP &p) const {
        float dx = M[0] - p.M[0];
        float dy = M[1] - p.M[1];
        return sqrtf(dx*dx + dy*dy);
    }

#ifdef DD_DEBUG
    /**
     * Formats the point as "(x.xxx, y.yyy)" into a caller-supplied buffer. Compiled only when DD_DEBUG is defined.
     * @param buf  Destination buffer.
     * @param n    Size of buf in bytes (output is truncated, never overrun).
     * @return     buf, for chaining into printf-style calls.
     * @warning Float formatting is slow and pulls in a large chunk of libc on microcontrollers. It is gated behind
     *          DD_DEBUG so production builds do not carry it.
     */
    char *ToString(char *buf, size_t n) const { snprintf(buf, n, "(%.3f, %.3f)", (double)M[0], (double)M[1]); return buf; }
#endif

    /**
     * Transforms the point in place: `p *= m` sets p to m applied to p.
     * @param m  The transform.
     * @return   *this (so it chains: `(p *= a) *= b` applies a then b).
     * @note `p * m` and `m * p` mean the same thing in dd (the matrix is always applied to the point).
     */
    ddP& operator *= (const Matrix3x3 &m);      // in place
    /**
     * Returns the point transformed by m, leaving this unchanged. Identical to `m * p`.
     * @param m  The transform.
     */
    ddP  operator *  (const Matrix3x3 &m) const; // returns new point
    /**
     * Component-wise sum, returned as a new point (weight 1). Treats points as vectors.
     */
    ddP  operator +  (const ddP &o) const { return { M[0] + o.M[0], M[1] + o.M[1] }; }
    /**
     * Component-wise difference (this - o) as a new point (weight 1). Use it to get the direction vector between two points.
     */
    ddP  operator -  (const ddP &o) const { return { M[0] - o.M[0], M[1] - o.M[1] }; }
    /**
     * Mutable element access: 0 = x, 1 = y, 2 = weight.
     * @warning No bounds check. Indices other than 0..2 are undefined behaviour.
     */
    float&       operator [] (int i)       { return M[i]; }
    /**
     * Read-only element access: 0 = x, 1 = y, 2 = weight.
     */
    const float& operator [] (int i) const { return M[i]; }
};

#ifdef DD_DEBUG
/**
 * Temporary string for printing a point inline. Compiled only with DD_DEBUG.
 * @code
 * printf("p = %s\n", (const char *)ddStr(p));
 * @endcode
 * @warning The text lives in the temporary: use it inside the same full expression, never store the const char*.
 */
struct ddStr {
    char Buf[40];
    /**
     * Formats a point (as ddP::ToString) into the temporary's own buffer. Explicit: write `ddStr(p)` where a string is needed.
     */
    explicit ddStr(const ddP &p) { p.ToString(Buf, sizeof(Buf)); }
    operator const char *() const { return Buf; }
};
#endif

/**
 * 3x3 matrix for 2D affine transforms (row-major, applied to column vectors).
 *
 * The bottom row is (0, 0, 1) for every matrix dd builds, so a point is transformed by
 * `x' = m00*x + m01*y + m02`, `y' = m10*x + m11*y + m12`.
 * Compose with `a * b`: the result applies b FIRST, then a (like function composition).
 *
 * @code
 * Matrix3x3 move(Matrix3x3::TransformType::TRANSLATION, 10, 0);
 * Matrix3x3 spin(Matrix3x3::TransformType::ROTATION, DDD_DEG2RAD(90));
 * ddP p(1, 0);
 * ddP q = (move * spin) * p;     // rotate first (to 0,1), then translate (to 10,1)
 * @endcode
 *
 * @warning Matrix multiplication is not commutative: `move * spin` is not `spin * move`.
 * @warning The default constructor gives the identity, but `float M[3][3]` is public and a matrix you fill by hand
 *          must keep the bottom row (0, 0, 1) or affine code (e.g. ddT, culling bounds) will misbehave.
 */
struct Matrix3x3 {
    float M[3][3];

    /**
     * Selects what the convenience constructor builds.
     */
    enum struct TransformType { TRANSLATION, ROTATION, SCALE };

    /**
     * Constructs the identity matrix (no change to points).
     */
    Matrix3x3() { SetIdentity(); }
    /**
     * Constructs a single translation, rotation or scale.
     * @param type  TRANSLATION, ROTATION or SCALE.
     * @param x     TRANSLATION: x offset. ROTATION: the angle in RADIANS. SCALE: x factor.
     * @param y     TRANSLATION: y offset. SCALE: y factor. Ignored for ROTATION.
     */
    Matrix3x3(TransformType type, float x = 0.0f, float y = 0.0f) {
        SetIdentity();
        switch (type) {
            case TransformType::TRANSLATION: SetTranslation(x, y); break;
            case TransformType::ROTATION:    SetRotation(x);       break;
            case TransformType::SCALE:       SetScale(x, y);       break;
        }
    }

    /**
     * Resets to the identity matrix.
     */
    void SetIdentity() { memset(M, 0, sizeof(M)); M[0][0] = M[1][1] = M[2][2] = 1.0f; }

    /**
     * Writes only the translation cells (m02, m12); the 2x2 part is untouched.
     * @warning SetRotation and SetScale both write the 2x2 block, so calling both on ONE matrix keeps only the last.
     *          Build one matrix per effect and multiply them, or use ddT, which does that for you.
     */
    void SetTranslation(float x, float y) { M[0][2] = x; M[1][2] = y; }
    /**
     * Writes the 2x2 block as a counter-clockwise rotation (y up).
     * @param A  Angle in radians.
     */
    void SetRotation(float A) { float c = cosf(A), s = sinf(A); M[0][0] = c; M[0][1] = -s; M[1][0] = s; M[1][1] = c; }
    /**
     * Writes the 2x2 block as a scale about the origin.
     * @param x  X factor.
     * @param y  Y factor. Negative values mirror.
     */
    void SetScale(float x, float y) { M[0][0] = x; M[1][1] = y; }

    /**
     * Inverts the matrix in place (full 3x3 adjugate/determinant, so translation is inverted correctly).
     * @warning If the matrix is singular (determinant exactly 0, e.g. a scale of 0) it is left UNCHANGED and nothing signals
     *          the failure. Check scales before inverting if that can happen.
     * @note Used to turn a world-to-display matrix into display-to-world (ddR::D2W).
     */
    void Invert() {
        float c00 =   M[1][1]*M[2][2] - M[1][2]*M[2][1];
        float c01 = -(M[1][0]*M[2][2] - M[1][2]*M[2][0]);
        float c02 =   M[1][0]*M[2][1] - M[1][1]*M[2][0];
        float det = M[0][0]*c00 + M[0][1]*c01 + M[0][2]*c02;
        if (det == 0.0f) return;
        float d = 1.0f / det;

        float inv[3][3];
        inv[0][0] = c00 * d;
        inv[1][0] = c01 * d;
        inv[2][0] = c02 * d;
        inv[0][1] = -(M[0][1]*M[2][2] - M[0][2]*M[2][1]) * d;
        inv[1][1] =  (M[0][0]*M[2][2] - M[0][2]*M[2][0]) * d;
        inv[2][1] = -(M[0][0]*M[2][1] - M[0][1]*M[2][0]) * d;
        inv[0][2] =  (M[0][1]*M[1][2] - M[0][2]*M[1][1]) * d;
        inv[1][2] = -(M[0][0]*M[1][2] - M[0][2]*M[1][0]) * d;
        inv[2][2] =  (M[0][0]*M[1][1] - M[0][1]*M[1][0]) * d;
        memcpy(M, inv, sizeof(M));
    }

    /**
     * Applies the matrix to a point and returns the result.
     * @param p  The point.
     * @return   The transformed point (all three components computed, so the weight stays correct for perspective-style matrices).
     */
    ddP Apply(const ddP &p) const {
        ddP r;
        r.M[0] = M[0][0]*p.M[0] + M[0][1]*p.M[1] + M[0][2];
        r.M[1] = M[1][0]*p.M[0] + M[1][1]*p.M[1] + M[1][2];
        r.M[2] = M[2][0]*p.M[0] + M[2][1]*p.M[1] + M[2][2];
        return r;
    }
    /**
     * Same as Apply: `m * p`.
     */
    ddP operator * (const ddP &p) const { return Apply(p); }

    /**
     * Matrix product `this * o`: applies o first, then this.
     * @param o  The transform applied first.
     * @return   The combined transform.
     * @note 27 multiply-adds. Per-frame code should combine once and reuse the result.
     */
    Matrix3x3 operator * (const Matrix3x3 &o) const {
        Matrix3x3 r;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                float s = 0.0f;
                for (int k = 0; k < 3; k++) s += M[i][k] * o.M[k][j];
                r.M[i][j] = s;
            }
        return r;
    }

#ifdef DD_DEBUG
    /**
     * Formats the 3x3 matrix as three lines of numbers into a caller-supplied buffer (DD_DEBUG only).
     * @param buf  Destination buffer.
     * @param n    Size of buf in bytes (output is truncated, never overrun).
     * @return     buf.
     */
    char *ToString(char *buf, size_t n) const {
        snprintf(buf, n, "%4.3f, %4.3f, %4.3f\n%4.3f, %4.3f, %4.3f\n%4.3f, %4.3f, %4.3f",
                 (double)M[0][0], (double)M[0][1], (double)M[0][2],
                 (double)M[1][0], (double)M[1][1], (double)M[1][2],
                 (double)M[2][0], (double)M[2][1], (double)M[2][2]);
        return buf;
    }
#endif
};

inline ddP& ddP::operator *= (const Matrix3x3 &m) { *this = m.Apply(*this); return *this; }
inline ddP  ddP::operator *  (const Matrix3x3 &m) const { return m.Apply(*this); }

/**
 * Position / angle / scale transform that builds its matrix on demand: M = Translation * Rotation * Scale.
 *
 * Scale is applied first (about the local origin), then rotation, then translation. Set the five components with the
 * setters, then call Combine() to rebuild `M`. Every scene primitive owns one (see ddVectorPrimitive::SetPos etc.).
 *
 * @code
 * ddT t;
 * t.SetScale(2, 2);
 * t.SetRotation(DDD_DEG2RAD(45));
 * t.SetTranslation(10, 5);
 * t.Combine();
 * ddP p = t * ddP(1, 0);        // scaled, rotated, then moved
 * @endcode
 *
 * @warning The setters change only the components. `M` is stale until you call Combine().
 * @note ddT is 56 bytes (five floats plus the matrix). Non-uniform scale combined with rotation produces a skew-free
 *       T*R*S (not a shear), which is what most drawing code expects.
 */
struct ddT {
    Matrix3x3 M;
    float Tx = 0.0f, Ty = 0.0f;   // translation
    float Angle = 0.0f;           // radians
    float Sx = 1.0f, Sy = 1.0f;   // scale

    /**
     * Resets position to (0,0), angle to 0, scale to (1,1) and `M` to identity.
     */
    void SetIdentity() { Tx = Ty = 0.0f; Angle = 0.0f; Sx = Sy = 1.0f; M.SetIdentity(); }
    /**
     * Sets the position component.
     * @warning Call Combine() afterwards.
     */
    void SetTranslation(float x, float y) { Tx = x; Ty = y; }
    /**
     * Sets the angle component.
     * @param A  Radians, counter-clockwise.
     * @warning Call Combine() afterwards.
     */
    void SetRotation(float A)             { Angle = A; }
    /**
     * Sets the scale component.
     * @warning Call Combine() afterwards.
     */
    void SetScale(float x, float y)       { Sx = x; Sy = y; }

    /**
     * Rebuilds `M` from the components (one sin and one cos, no matrix products).
     * @param Invert  true: leave the INVERSE transform in M (maps back from the transformed space).
     * @note Cheap enough to call every frame for animated objects; scene primitives do it lazily, once per frame.
     */
    void Combine(bool Invert = false) {
        float c = cosf(Angle), s = sinf(Angle);
        M.M[0][0] = c*Sx; M.M[0][1] = -s*Sy; M.M[0][2] = Tx;
        M.M[1][0] = s*Sx; M.M[1][1] =  c*Sy; M.M[1][2] = Ty;
        M.M[2][0] = 0.0f; M.M[2][1] = 0.0f;  M.M[2][2] = 1.0f;
        if (Invert) M.Invert();
    }

    /**
     * Applies the combined matrix to a point. Uses `M` as last built by Combine().
     */
    ddP operator * (const ddP &p) const { return M.Apply(p); }

#ifdef DD_DEBUG
    /**
     * Prints the matrix to stdout (debug only, needs DD_DEBUG).
     */
    void Print() const {
        for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) printf("%4.3f ", (double)M.M[i][j]); printf("\n"); }
    }
#endif
};
