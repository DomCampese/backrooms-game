#include "../core/fp_strict.h"
#include "model.h"
#include <cmath>
#include <cstring>
#include <map>
#include <memory>

namespace {

// ---- a JSON reader, enough for a glTF header

struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    double number = 0;
    bool boolean = false;
    std::string text;
    std::vector<Json> items;
    std::map<std::string, Json> fields;

    const Json &operator[](const char *key) const {
        static const Json none;
        auto it = fields.find(key);
        return it == fields.end() ? none : it->second;
    }
    const Json &at(size_t i) const {
        static const Json none;
        return i < items.size() ? items[i] : none;
    }
    bool has(const char *key) const { return fields.count(key) > 0; }
    int integer(int fallback = -1) const { return kind == Number ? (int)number : fallback; }
    float real(float fallback) const { return kind == Number ? (float)number : fallback; }
};

struct JsonReader {
    const char *p, *end;
    bool ok = true;

    void space() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
    bool take(char c) {
        space();
        if (p < end && *p == c) { p++; return true; }
        return false;
    }
    std::string string() {
        std::string s;
        if (!take('"')) { ok = false; return s; }
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                p++;
                switch (*p) {
                case 'n': s += '\n'; break;
                case 't': s += '\t'; break;
                case 'u': s += '?'; p += 4; break;   // no glTF name this reads uses one
                default: s += *p; break;
                }
                p++;
            } else s += *p++;
        }
        if (p >= end) ok = false;
        else p++;
        return s;
    }
    Json value() {
        Json v;
        space();
        if (p >= end) { ok = false; return v; }
        if (*p == '{') {
            p++;
            v.kind = Json::Object;
            if (take('}')) return v;
            do {
                std::string key = string();
                if (!take(':')) { ok = false; return v; }
                v.fields[key] = value();
            } while (ok && take(','));
            if (!take('}')) ok = false;
        } else if (*p == '[') {
            p++;
            v.kind = Json::Array;
            if (take(']')) return v;
            do v.items.push_back(value()); while (ok && take(','));
            if (!take(']')) ok = false;
        } else if (*p == '"') {
            v.kind = Json::String;
            v.text = string();
        } else if (!strncmp(p, "true", 4) && end - p >= 4) { v.kind = Json::Bool; v.boolean = true; p += 4; }
        else if (!strncmp(p, "false", 5) && end - p >= 5) { v.kind = Json::Bool; p += 5; }
        else if (!strncmp(p, "null", 4) && end - p >= 4) { p += 4; }
        else {
            char *stop = nullptr;
            std::string digits(p, (size_t)std::min<ptrdiff_t>(end - p, 64));
            v.number = strtod(digits.c_str(), &stop);
            if (stop == digits.c_str()) { ok = false; return v; }
            v.kind = Json::Number;
            p += stop - digits.c_str();
        }
        return v;
    }
};

uint32_t u32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }

// ---- raymath, operation for operation (raylib 5.5)

constexpr float EPS = 0.000001f;

Vec3 v3lerp(Vec3 a, Vec3 b, float t) { return { a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z) }; }
Vec3 v3mul(Vec3 a, Vec3 b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
Vec3 v3add(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }

GlbQuat qnormalize(GlbQuat q) {
    float length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length == 0.0f) length = 1.0f;
    float il = 1.0f / length;
    return { q.x * il, q.y * il, q.z * il, q.w * il };
}
GlbQuat qnlerp(GlbQuat a, GlbQuat b, float t) {
    GlbQuat r = { a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z), a.w + t * (b.w - a.w) };
    return qnormalize(r);
}
GlbQuat qslerp(GlbQuat q1, GlbQuat q2, float amount) {
    GlbQuat result = { 0, 0, 0, 0 };
    float cosHalfTheta = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;
    if (cosHalfTheta < 0) {
        q2.x = -q2.x; q2.y = -q2.y; q2.z = -q2.z; q2.w = -q2.w;
        cosHalfTheta = -cosHalfTheta;
    }
    if (fabsf(cosHalfTheta) >= 1.0f) result = q1;
    else if (cosHalfTheta > 0.95f) result = qnlerp(q1, q2, amount);
    else {
        float halfTheta = acosf(cosHalfTheta);
        float sinHalfTheta = sqrtf(1.0f - cosHalfTheta * cosHalfTheta);
        if (fabsf(sinHalfTheta) < EPS) {
            result = { q1.x * 0.5f + q2.x * 0.5f, q1.y * 0.5f + q2.y * 0.5f, q1.z * 0.5f + q2.z * 0.5f, q1.w * 0.5f + q2.w * 0.5f };
        } else {
            float ratioA = sinf((1 - amount) * halfTheta) / sinHalfTheta;
            float ratioB = sinf(amount * halfTheta) / sinHalfTheta;
            result = { q1.x * ratioA + q2.x * ratioB, q1.y * ratioA + q2.y * ratioB, q1.z * ratioA + q2.z * ratioB,
                       q1.w * ratioA + q2.w * ratioB };
        }
    }
    return result;
}
GlbQuat qinvert(GlbQuat q) {
    GlbQuat r = q;
    float lengthSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (lengthSq != 0.0f) {
        float inv = 1.0f / lengthSq;
        r.x *= -inv; r.y *= -inv; r.z *= -inv; r.w *= inv;
    }
    return r;
}

// raylib's Matrix: m[0..15] as m0..m15, column-major, a point transformed as
// x' = m0 x + m4 y + m8 z + m12.
struct Mat { float m[16]; };
Mat matIdentity() {
    Mat r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}
Mat matMultiply(const Mat &l, const Mat &rt) {
    Mat r{};
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++)
            r.m[row * 4 + col] = l.m[row * 4 + 0] * rt.m[0 * 4 + col] + l.m[row * 4 + 1] * rt.m[1 * 4 + col] +
                                 l.m[row * 4 + 2] * rt.m[2 * 4 + col] + l.m[row * 4 + 3] * rt.m[3 * 4 + col];
    return r;
}
Mat matTranslate(Vec3 t) {
    Mat r = matIdentity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
Mat matScale(Vec3 s) {
    Mat r = matIdentity();
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}
Mat matFromQuat(GlbQuat q) {
    Mat r = matIdentity();
    float a2 = q.x * q.x, b2 = q.y * q.y, c2 = q.z * q.z;
    float ac = q.x * q.z, ab = q.x * q.y, bc = q.y * q.z;
    float ad = q.w * q.x, bd = q.w * q.y, cd = q.w * q.z;
    r.m[0] = 1 - 2 * (b2 + c2); r.m[1] = 2 * (ab + cd); r.m[2] = 2 * (ac - bd);
    r.m[4] = 2 * (ab - cd); r.m[5] = 1 - 2 * (a2 + c2); r.m[6] = 2 * (bc + ad);
    r.m[8] = 2 * (ac + bd); r.m[9] = 2 * (bc - ad); r.m[10] = 1 - 2 * (a2 + b2);
    return r;
}
Vec3 matApply(const Mat &mat, Vec3 v) {
    return { mat.m[0] * v.x + mat.m[4] * v.y + mat.m[8] * v.z + mat.m[12],
             mat.m[1] * v.x + mat.m[5] * v.y + mat.m[9] * v.z + mat.m[13],
             mat.m[2] * v.x + mat.m[6] * v.y + mat.m[10] * v.z + mat.m[14] };
}

bool floatEquals(float x, float y) {
    return fabsf(x - y) <= EPS * fmaxf(1.0f, fmaxf(fabsf(x), fabsf(y)));
}

// ---- accessors

struct Accessor {
    const uint8_t *base = nullptr;
    size_t count = 0, stride = 0;
    int components = 0, componentType = 0;
    bool valid = false;
};

int componentsOf(const std::string &type) {
    return type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : type == "MAT4" ? 16 : 0;
}
size_t bytesOf(int componentType) {
    return componentType == 5126 || componentType == 5125 ? 4 : componentType == 5123 || componentType == 5122 ? 2 : 1;
}

Accessor accessor(const Json &gltf, const uint8_t *bin, size_t binSize, int index) {
    Accessor a;
    const Json &acc = gltf["accessors"].at((size_t)index);
    const Json &view = gltf["bufferViews"].at((size_t)acc["bufferView"].integer(-1));
    if (acc.kind != Json::Object || view.kind != Json::Object || view["buffer"].integer(0) != 0) return a;
    a.count = (size_t)acc["count"].integer(0);
    a.components = componentsOf(acc["type"].text);
    a.componentType = acc["componentType"].integer(0);
    size_t element = bytesOf(a.componentType) * (size_t)a.components;
    a.stride = view.has("byteStride") ? (size_t)view["byteStride"].integer(0) : element;
    size_t offset = (size_t)view["byteOffset"].integer(0) + (size_t)acc["byteOffset"].integer(0);
    if (a.components == 0 || a.count == 0 || offset + (a.count - 1) * a.stride + element > binSize) return a;
    a.base = bin + offset;
    a.valid = true;
    return a;
}

float readFloat(const Accessor &a, size_t i, int c) {
    float v;
    memcpy(&v, a.base + i * a.stride + (size_t)c * 4, 4);
    return v;
}
uint32_t readUint(const Accessor &a, size_t i, int c) {
    const uint8_t *p = a.base + i * a.stride + (size_t)c * bytesOf(a.componentType);
    if (a.componentType == 5121) return p[0];
    if (a.componentType == 5123) return (uint32_t)(p[0] | (p[1] << 8));
    return u32(p);
}

// GetPoseAtTimeGLTF: the value at `time`, between the two keys round it, or
// false where raylib leaves the joint's own value.
struct Channel { Accessor input, output; bool linear = true; bool present = false; };
bool channelAt(const Channel &ch, float time, float *out, int n) {
    float tstart = 0.0f, tend = 0.0f;
    size_t keyframe = 0;
    for (size_t i = 0; i + 1 < ch.input.count; i++) {
        tstart = readFloat(ch.input, i, 0);
        tend = readFloat(ch.input, i + 1, 0);
        if (tstart <= time && time < tend) { keyframe = i; break; }
    }
    if (floatEquals(tend, tstart)) return false;
    float duration = fmaxf(tend - tstart, EPS);
    float t = (time - tstart) / duration;
    t = t < 0.0f ? 0.0f : t;
    t = t > 1.0f ? 1.0f : t;
    if (!ch.linear) {
        for (int c = 0; c < n; c++) out[c] = readFloat(ch.output, keyframe, c);
        return true;
    }
    if (n == 3) {
        Vec3 a = { readFloat(ch.output, keyframe, 0), readFloat(ch.output, keyframe, 1), readFloat(ch.output, keyframe, 2) };
        Vec3 b = { readFloat(ch.output, keyframe + 1, 0), readFloat(ch.output, keyframe + 1, 1),
                   readFloat(ch.output, keyframe + 1, 2) };
        Vec3 r = v3lerp(a, b, t);
        out[0] = r.x; out[1] = r.y; out[2] = r.z;
    } else {
        GlbQuat a = { readFloat(ch.output, keyframe, 0), readFloat(ch.output, keyframe, 1), readFloat(ch.output, keyframe, 2),
                   readFloat(ch.output, keyframe, 3) };
        GlbQuat b = { readFloat(ch.output, keyframe + 1, 0), readFloat(ch.output, keyframe + 1, 1),
                   readFloat(ch.output, keyframe + 1, 2), readFloat(ch.output, keyframe + 1, 3) };
        GlbQuat r = qslerp(a, b, t);
        out[0] = r.x; out[1] = r.y; out[2] = r.z; out[3] = r.w;
    }
    return true;
}

GlbPose nodePose(const Json &node) {
    GlbPose p = { { 0, 0, 0 }, { 0, 0, 0, 1 }, { 1, 1, 1 } };
    const Json &t = node["translation"], &r = node["rotation"], &s = node["scale"];
    if (t.items.size() == 3) p.translation = { t.at(0).real(0), t.at(1).real(0), t.at(2).real(0) };
    if (r.items.size() == 4) p.rotation = { r.at(0).real(0), r.at(1).real(0), r.at(2).real(0), r.at(3).real(1) };
    if (s.items.size() == 3) p.scale = { s.at(0).real(1), s.at(1).real(1), s.at(2).real(1) };
    return p;
}

// BuildPoseFromParentJoints, for one bone whose parent is already done.
void composeWithParent(GlbPose &child, const GlbPose &parent) {
    child.rotation = quatMultiply(parent.rotation, child.rotation);
    child.translation = quatRotate(child.translation, parent.rotation);
    child.translation = v3add(child.translation, parent.translation);
    child.scale = v3mul(child.scale, parent.scale);
}

constexpr float ANIM_STEP_MS = 17.0f;   // raylib's GLTF_ANIMDELAY

}   // namespace

Vec3 quatRotate(Vec3 v, GlbQuat q) {
    Vec3 r;
    r.x = v.x * (q.x * q.x + q.w * q.w - q.y * q.y - q.z * q.z) + v.y * (2 * q.x * q.y - 2 * q.w * q.z) + v.z * (2 * q.x * q.z + 2 * q.w * q.y);
    r.y = v.x * (2 * q.w * q.z + 2 * q.x * q.y) + v.y * (q.w * q.w - q.x * q.x + q.y * q.y - q.z * q.z) + v.z * (-2 * q.w * q.x + 2 * q.y * q.z);
    r.z = v.x * (-2 * q.w * q.y + 2 * q.x * q.z) + v.y * (2 * q.w * q.x + 2 * q.y * q.z) + v.z * (q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z);
    return r;
}

GlbQuat quatMultiply(GlbQuat q1, GlbQuat q2) {
    float qax = q1.x, qay = q1.y, qaz = q1.z, qaw = q1.w;
    float qbx = q2.x, qby = q2.y, qbz = q2.z, qbw = q2.w;
    return { qax * qbw + qaw * qbx + qay * qbz - qaz * qby, qay * qbw + qaw * qby + qaz * qbx - qax * qbz,
             qaz * qbw + qaw * qbz + qax * qby - qay * qbx, qaw * qbw - qax * qbx - qay * qby - qaz * qbz };
}

GlbQuat quatFromAxisAngle(Vec3 axis, float angle) {
    GlbQuat result = { 0.0f, 0.0f, 0.0f, 1.0f };
    float axisLength = sqrtf(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (axisLength != 0.0f) {
        angle *= 0.5f;
        float length = axisLength;
        if (length == 0.0f) length = 1.0f;
        float ilength = 1.0f / length;
        axis.x *= ilength; axis.y *= ilength; axis.z *= ilength;
        float sinres = sinf(angle), cosres = cosf(angle);
        result = { axis.x * sinres, axis.y * sinres, axis.z * sinres, cosres };
        result = qnormalize(result);
    }
    return result;
}

bool loadGlb(const uint8_t *data, size_t size, GlbModel &out, std::string &error) {
    out = GlbModel();
    if (size < 20 || memcmp(data, "glTF", 4) != 0 || u32(data + 4) != 2) { error = "not a glTF 2 binary"; return false; }
    size_t jsonSize = u32(data + 12);
    if (u32(data + 16) != 0x4E4F534A || 20 + jsonSize > size) { error = "no JSON chunk"; return false; }
    JsonReader reader{ (const char *)data + 20, (const char *)data + 20 + jsonSize };
    const Json gltf = reader.value();
    if (!reader.ok || gltf.kind != Json::Object) { error = "the JSON chunk does not parse"; return false; }
    size_t binAt = 20 + ((jsonSize + 3) & ~(size_t)3);
    if (binAt + 8 > size || u32(data + binAt + 4) != 0x004E4942) { error = "no binary chunk"; return false; }
    const uint8_t *bin = data + binAt + 8;
    const size_t binSize = std::min((size_t)u32(data + binAt), size - binAt - 8);

    for (const Json &img : gltf["images"].items) {
        GlbModel::Image image;
        const Json &view = gltf["bufferViews"].at((size_t)img["bufferView"].integer(-1));
        image.offset = (size_t)(bin - data) + (size_t)view["byteOffset"].integer(0);
        image.size = (size_t)view["byteLength"].integer(0);
        image.mime = img["mimeType"].text;
        if (image.offset + image.size > size) { error = "an image runs past the file"; return false; }
        out.images.push_back(image);
    }
    auto imageOf = [&](const Json &ref) {
        if (ref.kind != Json::Object) return -1;
        return gltf["textures"].at((size_t)ref["index"].integer(-1))["source"].integer(-1);
    };
    for (const Json &mat : gltf["materials"].items) {
        GlbMaterial m;
        m.baseColour = imageOf(mat["pbrMetallicRoughness"]["baseColorTexture"]);
        m.metalRough = imageOf(mat["pbrMetallicRoughness"]["metallicRoughnessTexture"]);
        m.normal = imageOf(mat["normalTexture"]);
        out.materials.push_back(m);
    }

    // The skin: raylib's bones and their bind pose, each joint's world pose.
    const Json &nodes = gltf["nodes"];
    std::vector<int> nodeParent(nodes.items.size(), -1);
    for (size_t n = 0; n < nodes.items.size(); n++)
        for (const Json &c : nodes.at(n)["children"].items)
            if (c.integer(-1) >= 0 && (size_t)c.integer(-1) < nodeParent.size()) nodeParent[(size_t)c.integer(-1)] = (int)n;
    const Json &skin = gltf["skins"].at(0);
    std::vector<int> jointNodes;
    for (const Json &j : skin["joints"].items) jointNodes.push_back(j.integer(-1));
    for (size_t k = 0; k < jointNodes.size(); k++) {
        const Json &node = nodes.at((size_t)jointNodes[k]);
        out.bones.push_back(node["name"].text);
        int parent = -1;
        for (size_t q = 0; q < jointNodes.size(); q++) if (jointNodes[q] == nodeParent[(size_t)jointNodes[k]]) parent = (int)q;
        out.parent.push_back(parent);
        // world pose: the node's own, composed up through every ancestor
        std::vector<int> chain;
        for (int n = jointNodes[k]; n >= 0; n = nodeParent[(size_t)n]) chain.push_back(n);
        GlbPose world = nodePose(nodes.at((size_t)chain.back()));
        for (size_t c = chain.size() - 1; c-- > 0;) {
            GlbPose p = nodePose(nodes.at((size_t)chain[c]));
            composeWithParent(p, world);
            world = p;
        }
        out.bindPose.push_back(world);
    }

    // Meshes, in raylib's order.
    for (const Json &node : nodes.items) {
        if (!node.has("mesh")) continue;
        for (const Json &prim : gltf["meshes"].at((size_t)node["mesh"].integer(-1))["primitives"].items) {
            if (prim["mode"].integer(4) != 4) continue;
            GlbPrimitive p;
            p.material = prim["material"].integer(-1);
            const Json &at = prim["attributes"];
            Accessor pos = accessor(gltf, bin, binSize, at["POSITION"].integer(-1));
            Accessor nrm = accessor(gltf, bin, binSize, at["NORMAL"].integer(-1));
            Accessor uv = accessor(gltf, bin, binSize, at["TEXCOORD_0"].integer(-1));
            Accessor joints = accessor(gltf, bin, binSize, at["JOINTS_0"].integer(-1));
            Accessor weights = accessor(gltf, bin, binSize, at["WEIGHTS_0"].integer(-1));
            Accessor idx = accessor(gltf, bin, binSize, prim["indices"].integer(-1));
            if (!pos.valid || pos.componentType != 5126 || !nrm.valid || !uv.valid || !idx.valid || !joints.valid ||
                !weights.valid || weights.componentType != 5126) {
                error = "a primitive lacks float positions, normals, UVs, weights, joints or indices";
                return false;
            }
            for (size_t i = 0; i < pos.count; i++) {
                for (int c = 0; c < 3; c++) p.pos.push_back(readFloat(pos, i, c));
                for (int c = 0; c < 3; c++) p.normal.push_back(readFloat(nrm, i, c));
                for (int c = 0; c < 2; c++) p.uv.push_back(readFloat(uv, i, c));
                for (int c = 0; c < 4; c++) p.joints.push_back((uint8_t)readUint(joints, i, c));
                for (int c = 0; c < 4; c++) p.weights.push_back(readFloat(weights, i, c));
            }
            for (size_t i = 0; i < idx.count; i++) p.index.push_back(readUint(idx, i, 0));
            out.prims.push_back(std::move(p));
        }
    }

    // Clips, sampled as LoadModelAnimationsGLTF samples them.
    for (const Json &anim : gltf["animations"].items) {
        GlbClip clip;
        clip.name = anim["name"].text;
        std::vector<Channel> trans(jointNodes.size()), rot(jointNodes.size()), scl(jointNodes.size());
        std::vector<bool> linear(jointNodes.size(), true);
        float duration = 0.0f;
        for (const Json &ch : anim["channels"].items) {
            int node = ch["target"]["node"].integer(-1);
            int bone = -1;
            for (size_t k = 0; k < jointNodes.size(); k++) if (jointNodes[k] == node) bone = (int)k;
            if (bone < 0) continue;
            const Json &sampler = anim["samplers"].at((size_t)ch["sampler"].integer(-1));
            const std::string interp = sampler.has("interpolation") ? sampler["interpolation"].text : "LINEAR";
            if (interp == "CUBICSPLINE") { error = "cubic spline channels are not read"; return false; }
            linear[(size_t)bone] = interp == "LINEAR";   // the bone's last channel decides, as in raylib
            Channel c;
            c.input = accessor(gltf, bin, binSize, sampler["input"].integer(-1));
            c.output = accessor(gltf, bin, binSize, sampler["output"].integer(-1));
            c.present = c.input.valid && c.output.valid;
            if (!c.present) continue;
            const std::string &path = ch["target"]["path"].text;
            if (path == "translation") trans[(size_t)bone] = c;
            else if (path == "rotation") rot[(size_t)bone] = c;
            else if (path == "scale") scl[(size_t)bone] = c;
            float last = readFloat(c.input, c.input.count - 1, 0);
            duration = last > duration ? last : duration;
        }
        for (size_t k = 0; k < jointNodes.size(); k++) trans[k].linear = rot[k].linear = scl[k].linear = linear[k];
        const int frames = (int)(duration * 1000.0f / ANIM_STEP_MS) + 1;
        for (int f = 0; f < frames; f++) {
            float time = ((float)f * ANIM_STEP_MS) / 1000.0f;
            std::vector<GlbPose> pose(jointNodes.size());
            for (size_t k = 0; k < jointNodes.size(); k++) {
                GlbPose p = nodePose(nodes.at((size_t)jointNodes[k]));
                float v[4];
                if (trans[k].present && channelAt(trans[k], time, v, 3)) p.translation = { v[0], v[1], v[2] };
                if (rot[k].present && channelAt(rot[k], time, v, 4)) p.rotation = { v[0], v[1], v[2], v[3] };
                if (scl[k].present && channelAt(scl[k], time, v, 3)) p.scale = { v[0], v[1], v[2] };
                pose[k] = p;
            }
            for (size_t k = 0; k < pose.size(); k++)
                if (out.parent[k] >= 0 && out.parent[k] < (int)k) composeWithParent(pose[k], pose[(size_t)out.parent[k]]);
            clip.frames.push_back(std::move(pose));
        }
        out.clips.push_back(std::move(clip));
    }
    return true;
}

void glbDetailMap(const uint8_t *normal, const uint8_t *metalRough, int pixels, uint8_t *out) {
    auto byte = [](float v) { return (uint8_t)(v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v); };
    for (int i = 0; i < pixels; ++i) {
        // read before writing: `out` may be `normal`
        const int nr = normal[i * 4], ng = normal[i * 4 + 1], nb = normal[i * 4 + 2];
        const int rg = metalRough[i * 4 + 1], rb = metalRough[i * 4 + 2];
        float z = fmaxf(nb / 127.5f - 1, .25f);
        out[i * 4] = byte(128 - 127 * (nr / 127.5f - 1) / z);
        out[i * 4 + 1] = byte(128 - 127 * (ng / 127.5f - 1) / z);
        out[i * 4 + 2] = byte((1 - rg / 255.0f) * (.18f + .72f * rb / 255.0f) * 255);
        out[i * 4 + 3] = byte(128 + 60 * rb / 255.0f);
    }
}

int glbClip(const GlbModel &m, const char *name) {
    for (size_t i = 0; i < m.clips.size(); i++) if (m.clips[i].name == name) return (int)i;
    return -1;
}

int glbBone(const GlbModel &m, const char *name) {
    for (size_t i = 0; i < m.bones.size(); i++) if (m.bones[i] == name) return (int)i;
    return -1;
}

void glbSample(const GlbModel &m, int clip, float progress, std::vector<GlbPose> &out) {
    const GlbClip &c = m.clips[(size_t)clip];
    const int frames = (int)c.frames.size();
    float frame = clampf(progress, 0.0f, 1.0f) * (frames - 1);
    int a = (int)frame, b = a + 1 < frames - 1 ? a + 1 : frames - 1;
    float f = frame - a;
    out.resize(m.bones.size());
    for (size_t i = 0; i < m.bones.size(); i++) {
        const GlbPose &x = c.frames[(size_t)a][i], &y = c.frames[(size_t)b][i];
        out[i] = { v3lerp(x.translation, y.translation, f), qslerp(x.rotation, y.rotation, f), v3lerp(x.scale, y.scale, f) };
    }
}

void glbSkin(const GlbModel &m, const std::vector<GlbPose> &pose, int prim, std::vector<float> &pos,
             std::vector<float> &normal) {
    // UpdateModelAnimationBones: rotate, then translate, then scale
    std::vector<Mat> bones(m.bones.size());
    for (size_t b = 0; b < m.bones.size(); b++) {
        const GlbPose &in = m.bindPose[b], &o = pose[b];
        Vec3 negT = { -in.translation.x, -in.translation.y, -in.translation.z };
        Vec3 invTranslation = quatRotate(negT, qinvert(in.rotation));
        GlbQuat invRotation = qinvert(in.rotation);
        Vec3 invScale = { 1.0f / in.scale.x, 1.0f / in.scale.y, 1.0f / in.scale.z };
        Vec3 boneTranslation = v3add(quatRotate(v3mul(o.scale, invTranslation), o.rotation), o.translation);
        GlbQuat boneRotation = quatMultiply(o.rotation, invRotation);
        Vec3 boneScale = v3mul(o.scale, invScale);
        bones[b] = matMultiply(matMultiply(matFromQuat(boneRotation), matTranslate(boneTranslation)), matScale(boneScale));
    }
    const GlbPrimitive &p = m.prims[(size_t)prim];
    const size_t n = p.pos.size() / 3;
    pos.assign(n * 3, 0.0f);
    normal.assign(n * 3, 0.0f);
    for (size_t v = 0; v < n; v++) {
        Vec3 offset = { 0, 0, 0 };
        for (int j = 0; j < 4; j++) {
            float w = p.weights[v * 4 + (size_t)j];
            if (w == 0.0f) continue;
            const Mat &bm = bones[p.joints[v * 4 + (size_t)j]];
            Vec3 av = matApply(bm, { p.pos[v * 3], p.pos[v * 3 + 1], p.pos[v * 3 + 2] });
            pos[v * 3] += av.x * w;
            pos[v * 3 + 1] += av.y * w;
            pos[v * 3 + 2] += av.z * w;
            // raylib 5.5 moves normals as points; ModelAsset::update takes the
            // weighted translation back out and renormalizes
            Vec3 an = matApply(bm, { p.normal[v * 3], p.normal[v * 3 + 1], p.normal[v * 3 + 2] });
            normal[v * 3] += an.x * w;
            normal[v * 3 + 1] += an.y * w;
            normal[v * 3 + 2] += an.z * w;
            offset = v3add(offset, Vec3{ bm.m[12] * w, bm.m[13] * w, bm.m[14] * w });
        }
        Vec3 d = { normal[v * 3] - offset.x, normal[v * 3 + 1] - offset.y, normal[v * 3 + 2] - offset.z };
        float length = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        if (length != 0.0f) {
            float il = 1.0f / length;
            d.x *= il; d.y *= il; d.z *= il;
        }
        normal[v * 3] = d.x; normal[v * 3 + 1] = d.y; normal[v * 3 + 2] = d.z;
    }
}
