#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

#define DDD_PI 3.14159265358979323846f
#define DDD_DEG2RAD(A) (((float)(A))*DDD_PI/180.0f)
#define DDD_RAD2DEG(A) (((float)(A))*180.0f/DDD_PI)

struct Matrix3x3;

// Homogeneous 2D point (x, y, 1).
struct ddP {
    float M[3] = {0.0f, 0.0f, 1.0f};

    ddP() = default;
    ddP(float x, float y) { Set(x, y); }

    void Set(float x = 0.0f, float y = 0.0f) { M[0] = x; M[1] = y; M[2] = 1.0f; }
    void Zero() { Set(0.0f, 0.0f); }

    float DistanceTo(const ddP &p) const {
        float dx = M[0] - p.M[0];
        float dy = M[1] - p.M[1];
        return sqrtf(dx*dx + dy*dy);
    }

#ifdef DD_DEBUG
    // Fills caller-supplied buffer (no shared static state).
    char *ToString(char *buf, size_t n) const { snprintf(buf, n, "(%.3f, %.3f)", (double)M[0], (double)M[1]); return buf; }
#endif

    ddP& operator *= (const Matrix3x3 &m);      // in place
    ddP  operator *  (const Matrix3x3 &m) const; // returns new point
    ddP  operator +  (const ddP &o) const { return { M[0] + o.M[0], M[1] + o.M[1] }; }
    ddP  operator -  (const ddP &o) const { return { M[0] - o.M[0], M[1] - o.M[1] }; }
    float&       operator [] (int i)       { return M[i]; }
    const float& operator [] (int i) const { return M[i]; }
};

#ifdef DD_DEBUG
// Small helper so ToString can be used inline in a call: f(ddStr(p));
struct ddStr {
    char Buf[40];
    explicit ddStr(const ddP &p) { p.ToString(Buf, sizeof(Buf)); }
    operator const char *() const { return Buf; }
};
#endif

struct Matrix3x3 {
    float M[3][3];

    enum struct TransformType { TRANSLATION, ROTATION, SCALE };

    Matrix3x3() { SetIdentity(); }
    Matrix3x3(TransformType type, float x = 0.0f, float y = 0.0f) {
        SetIdentity();
        switch (type) {
            case TransformType::TRANSLATION: SetTranslation(x, y); break;
            case TransformType::ROTATION:    SetRotation(x);       break;
            case TransformType::SCALE:       SetScale(x, y);       break;
        }
    }

    void SetIdentity() { memset(M, 0, sizeof(M)); M[0][0] = M[1][1] = M[2][2] = 1.0f; }

    // Each setter replaces only its own cells; use on separate matrices and multiply.
    void SetTranslation(float x, float y) { M[0][2] = x; M[1][2] = y; }
    void SetRotation(float A) { float c = cosf(A), s = sinf(A); M[0][0] = c; M[0][1] = -s; M[1][0] = s; M[1][1] = c; }
    void SetScale(float x, float y) { M[0][0] = x; M[1][1] = y; }

    // Full 3x3 inverse (adjugate / determinant). Unchanged if singular.
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

    ddP Apply(const ddP &p) const {
        ddP r;
        r.M[0] = M[0][0]*p.M[0] + M[0][1]*p.M[1] + M[0][2];
        r.M[1] = M[1][0]*p.M[0] + M[1][1]*p.M[1] + M[1][2];
        r.M[2] = M[2][0]*p.M[0] + M[2][1]*p.M[1] + M[2][2];
        return r;
    }
    ddP operator * (const ddP &p) const { return Apply(p); }

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

// Composite transform: M = Translation * Rotation * Scale, built directly from
// position / angle / scale (no intermediate matrices). Call Combine() after changing them.
struct ddT {
    Matrix3x3 M;
    float Tx = 0.0f, Ty = 0.0f;   // translation
    float Angle = 0.0f;           // radians
    float Sx = 1.0f, Sy = 1.0f;   // scale

    void SetIdentity() { Tx = Ty = 0.0f; Angle = 0.0f; Sx = Sy = 1.0f; M.SetIdentity(); }
    void SetTranslation(float x, float y) { Tx = x; Ty = y; }
    void SetRotation(float A)             { Angle = A; }
    void SetScale(float x, float y)       { Sx = x; Sy = y; }

    // M = T * R * S  =  [ c*Sx  -s*Sy  Tx ]
    //                   [ s*Sx   c*Sy  Ty ]
    //                   [  0      0     1 ]
    // If Invert, M becomes the inverse transform.
    void Combine(bool Invert = false) {
        float c = cosf(Angle), s = sinf(Angle);
        M.M[0][0] = c*Sx; M.M[0][1] = -s*Sy; M.M[0][2] = Tx;
        M.M[1][0] = s*Sx; M.M[1][1] =  c*Sy; M.M[1][2] = Ty;
        M.M[2][0] = 0.0f; M.M[2][1] = 0.0f;  M.M[2][2] = 1.0f;
        if (Invert) M.Invert();
    }

    ddP operator * (const ddP &p) const { return M.Apply(p); }

#ifdef DD_DEBUG
    void Print() const {
        for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) printf("%4.3f ", (double)M.M[i][j]); printf("\n"); }
    }
#endif
};
