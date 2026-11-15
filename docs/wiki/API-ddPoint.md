# ddPoint.h

*Points, matrices and transforms.*  Source: [`src/ddPoint.h`](https://github.com/JasonBolbach/dd/blob/main/src/ddPoint.h)

Everything here is plain 2D affine maths in homogeneous coordinates: a point is (x, y, 1) and a transform is a
3x3 matrix, so translation, rotation and scale all compose by matrix multiplication. The 3x3 form is deliberate:
it is what a 3D extension would generalise.

**Macros**

DDD_PI (float pi), DDD_DEG2RAD(a) and DDD_RAD2DEG(a) (both cast the argument to float, so integer degrees are fine).

**Tips and notes**

- Only `float` is used (never `double`), because most target microcontrollers have a single-precision FPU only.
- Angles are in RADIANS everywhere in dd. Use DDD_DEG2RAD / DDD_RAD2DEG to convert.

## Contents

- [struct ddP](#struct-ddp)
- [struct ddStr](#struct-ddstr)
- [struct Matrix3x3](#struct-matrix3x3)
- [enum TransformType](#enum-transformtype)
- [struct ddT](#struct-ddt)

## struct ddP

```cpp
struct ddP
```

Homogeneous 2D point (x, y, 1).

The workhorse value type of the library: 12 bytes, trivially copyable, safe to memcpy. Access the coordinates with
`p[0]` (x) and `p[1]` (y); `p[2]` is the homogeneous weight and is 1 for every point you construct.

```cpp
ddP a(1, 2), b(4, 6);
float d = a.DistanceTo(b);   // 5
ddP mid((a[0] + b[0]) / 2, (a[1] + b[1]) / 2);
```

**Pitfalls**

- `p[2]` is the homogeneous weight. Affine transforms keep it at 1 and the 2D code relies on that, so do not store other data in it.

**Tips and notes**

- Passing [`ddP`](API-ddPoint#struct-ddp) by value is cheap (12 bytes); the library does so for small helpers and uses `const ddP&` elsewhere.

---
### ddP::ddP()

```cpp
ddP() = default
```

Default constructor: the point (0, 0), weight 1.


### ddP::ddP() (overload 2)

```cpp
ddP(float x, float y)
```

Constructs the point (x, y).

| Parameter | Description |
|---|---|
| `x` | X coordinate (world units, or pixels - the type does not care). |
| `y` | Y coordinate. |


### ddP::Set()

```cpp
void Set(float x = 0.0f, float y = 0.0f)
```

Overwrites the point and resets the weight to 1.

| Parameter | Description |
|---|---|
| `x` | New X (default 0). |
| `y` | New Y (default 0). |


### ddP::Zero()

```cpp
void Zero()
```

Sets the point to the origin (0, 0). Same as Set(0, 0).

**Tips and notes**

- Named Zero (not SetIdentity) because a point has no identity, only an origin.


### ddP::DistanceTo()

```cpp
float DistanceTo(const ddP &p) const
```

Euclidean distance to another point.

| Parameter | Description |
|---|---|
| `p` | The other point. |

**Returns:** Distance, always >= 0.

**Tips and notes**

- Uses a square root. To only COMPARE distances, compare squared distances yourself and save the sqrtf.


### ddP::ToString()

```cpp
char *ToString(char *buf, size_t n) const
```

Formats the point as "(x.xxx, y.yyy)" into a caller-supplied buffer. Compiled only when DD_DEBUG is defined.

| Parameter | Description |
|---|---|
| `buf` | Destination buffer. |
| `n` | Size of buf in bytes (output is truncated, never overrun). |

**Returns:** buf, for chaining into printf-style calls.

**Pitfalls**

- Float formatting is slow and pulls in a large chunk of libc on microcontrollers. It is gated behind DD_DEBUG so production builds do not carry it.


### ddP::operator *=()

```cpp
ddP& operator *= (const Matrix3x3 &m)
```

Transforms the point in place: `p *= m` sets p to m applied to p.

| Parameter | Description |
|---|---|
| `m` | The transform. |

**Returns:** *this (so it chains: `(p *= a) *= b` applies a then b).

**Tips and notes**

- `p * m` and `m * p` mean the same thing in dd (the matrix is always applied to the point).


### ddP::operator *()

```cpp
ddP operator * (const Matrix3x3 &m) const
```

Returns the point transformed by m, leaving this unchanged. Identical to `m * p`.

| Parameter | Description |
|---|---|
| `m` | The transform. |


### ddP::operator +()

```cpp
ddP operator + (const ddP &o) const
```

Component-wise sum, returned as a new point (weight 1). Treats points as vectors.


### ddP::operator -()

```cpp
ddP operator - (const ddP &o) const
```

Component-wise difference (this - o) as a new point (weight 1). Use it to get the direction vector between two points.


### ddP::operator []()

```cpp
float& operator [] (int i)
```

Mutable element access: 0 = x, 1 = y, 2 = weight.

**Pitfalls**

- No bounds check. Indices other than 0..2 are undefined behaviour.


### ddP::operator []() (overload 2)

```cpp
const float& operator [] (int i) const
```

Read-only element access: 0 = x, 1 = y, 2 = weight.


## struct ddStr

```cpp
struct ddStr
```

Temporary string for printing a point inline. Compiled only with DD_DEBUG.

```cpp
printf("p = %s\n", (const char *)ddStr(p));
```

**Pitfalls**

- The text lives in the temporary: use it inside the same full expression, never store the const char*.

---
### ddStr::ddStr()

```cpp
explicit ddStr(const ddP &p)
```

Formats a point (as [ddP](API-ddPoint#struct-ddp)::ToString) into the temporary's own buffer. Explicit: write `ddStr(p)` where a string is needed.


## struct Matrix3x3

```cpp
struct Matrix3x3
```

3x3 matrix for 2D affine transforms (row-major, applied to column vectors).

The bottom row is (0, 0, 1) for every matrix dd builds, so a point is transformed by
`x' = m00*x + m01*y + m02`, `y' = m10*x + m11*y + m12`.
Compose with `a * b`: the result applies b FIRST, then a (like function composition).

```cpp
Matrix3x3 move(Matrix3x3::TransformType::TRANSLATION, 10, 0);
Matrix3x3 spin(Matrix3x3::TransformType::ROTATION, DDD_DEG2RAD(90));
ddP p(1, 0);
ddP q = (move * spin) * p;     // rotate first (to 0,1), then translate (to 10,1)
```

**Pitfalls**

- Matrix multiplication is not commutative: `move * spin` is not `spin * move`.
- The default constructor gives the identity, but `float M[3][3]` is public and a matrix you fill by hand must keep the bottom row (0, 0, 1) or affine code (e.g. [ddT](API-ddPoint#struct-ddt), culling bounds) will misbehave.

---
## enum TransformType

```cpp
enum struct TransformType
```

Selects what the convenience constructor builds.

---
### Matrix3x3::Matrix3x3()

```cpp
Matrix3x3()
```

Constructs the identity matrix (no change to points).


### Matrix3x3::Matrix3x3() (overload 2)

```cpp
Matrix3x3(TransformType type, float x = 0.0f, float y = 0.0f)
```

Constructs a single translation, rotation or scale.

| Parameter | Description |
|---|---|
| `type` | TRANSLATION, ROTATION or SCALE. |
| `x` | TRANSLATION: x offset. ROTATION: the angle in RADIANS. SCALE: x factor. |
| `y` | TRANSLATION: y offset. SCALE: y factor. Ignored for ROTATION. |


### Matrix3x3::SetIdentity()

```cpp
void SetIdentity()
```

Resets to the identity matrix.


### Matrix3x3::SetTranslation()

```cpp
void SetTranslation(float x, float y)
```

Writes only the translation cells (m02, m12); the 2x2 part is untouched.

**Pitfalls**

- SetRotation and SetScale both write the 2x2 block, so calling both on ONE matrix keeps only the last. Build one matrix per effect and multiply them, or use [ddT](API-ddPoint#struct-ddt), which does that for you.


### Matrix3x3::SetRotation()

```cpp
void SetRotation(float A)
```

Writes the 2x2 block as a counter-clockwise rotation (y up).

| Parameter | Description |
|---|---|
| `A` | Angle in radians. |


### Matrix3x3::SetScale()

```cpp
void SetScale(float x, float y)
```

Writes the 2x2 block as a scale about the origin.

| Parameter | Description |
|---|---|
| `x` | X factor. |
| `y` | Y factor. Negative values mirror. |


### Matrix3x3::Invert()

```cpp
void Invert()
```

Inverts the matrix in place (full 3x3 adjugate/determinant, so translation is inverted correctly).

**Pitfalls**

- If the matrix is singular (determinant exactly 0, e.g. a scale of 0) it is left UNCHANGED and nothing signals the failure. Check scales before inverting if that can happen.

**Tips and notes**

- Used to turn a world-to-display matrix into display-to-world ([ddR](API-ddRender#class-ddr)::D2W).


### Matrix3x3::Apply()

```cpp
ddP Apply(const ddP &p) const
```

Applies the matrix to a point and returns the result.

| Parameter | Description |
|---|---|
| `p` | The point. |

**Returns:** The transformed point (all three components computed, so the weight stays correct for perspective-style matrices).


### Matrix3x3::operator *()

```cpp
ddP operator * (const ddP &p) const
```

Same as Apply: `m * p`.


### Matrix3x3::operator *() (overload 2)

```cpp
Matrix3x3 operator * (const Matrix3x3 &o) const
```

Matrix product `this * o`: applies o first, then this.

| Parameter | Description |
|---|---|
| `o` | The transform applied first. |

**Returns:** The combined transform.

**Tips and notes**

- 27 multiply-adds. Per-frame code should combine once and reuse the result.


### Matrix3x3::ToString()

```cpp
char *ToString(char *buf, size_t n) const
```

Formats the 3x3 matrix as three lines of numbers into a caller-supplied buffer (DD_DEBUG only).

| Parameter | Description |
|---|---|
| `buf` | Destination buffer. |
| `n` | Size of buf in bytes (output is truncated, never overrun). |

**Returns:** buf.


## struct ddT

```cpp
struct ddT
```

Position / angle / scale transform that builds its matrix on demand: M = Translation * Rotation * Scale.

Scale is applied first (about the local origin), then rotation, then translation. Set the five components with the
setters, then call Combine() to rebuild `M`. Every scene primitive owns one (see [ddVectorPrimitive](API-ddVector#struct-ddvectorprimitive)::SetPos etc.).

```cpp
ddT t;
t.SetScale(2, 2);
t.SetRotation(DDD_DEG2RAD(45));
t.SetTranslation(10, 5);
t.Combine();
ddP p = t * ddP(1, 0);        // scaled, rotated, then moved
```

**Pitfalls**

- The setters change only the components. `M` is stale until you call Combine().

**Tips and notes**

- [ddT](API-ddPoint#struct-ddt) is 56 bytes (five floats plus the matrix). Non-uniform scale combined with rotation produces a skew-free T*R*S (not a shear), which is what most drawing code expects.

---
### ddT::SetIdentity()

```cpp
void SetIdentity()
```

Resets position to (0,0), angle to 0, scale to (1,1) and `M` to identity.


### ddT::SetTranslation()

```cpp
void SetTranslation(float x, float y)
```

Sets the position component.

**Pitfalls**

- Call Combine() afterwards.


### ddT::SetRotation()

```cpp
void SetRotation(float A)
```

Sets the angle component.

| Parameter | Description |
|---|---|
| `A` | Radians, counter-clockwise. |

**Pitfalls**

- Call Combine() afterwards.


### ddT::SetScale()

```cpp
void SetScale(float x, float y)
```

Sets the scale component.

**Pitfalls**

- Call Combine() afterwards.


### ddT::Combine()

```cpp
void Combine(bool Invert = false)
```

Rebuilds `M` from the components (one sin and one cos, no matrix products).

| Parameter | Description |
|---|---|
| `Invert` | true: leave the INVERSE transform in M (maps back from the transformed space). |

**Tips and notes**

- Cheap enough to call every frame for animated objects; scene primitives do it lazily, once per frame.


### ddT::operator *()

```cpp
ddP operator * (const ddP &p) const
```

Applies the combined matrix to a point. Uses `M` as last built by Combine().


### ddT::Print()

```cpp
void Print() const
```

Prints the matrix to stdout (debug only, needs DD_DEBUG).
