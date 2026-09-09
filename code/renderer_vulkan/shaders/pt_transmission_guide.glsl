// Same 32-byte allocation as the original position/normal transmission guide.
// position.xyz = real opaque target, position.w = packed octahedral normal.
// identity = target material/object ID, two-word ordered path signature, count.
// Count zero is invalid; packed normals/signatures are NEVER float validity flags.
struct TransmissionGuide { vec4 position; uvec4 identity; };
const int PT_TRANSMISSION_INTERFACES=4;

uvec2 transmissionSignature(uvec2 signature,uint word) {
    // Two independent 32-bit streams avoid requiring shaderInt64. This is a
    // rejection fingerprint, not a replacement for geometric/visibility tests.
    signature.x=(signature.x^word)*16777619u;
    signature.y=(signature.y+word)*2246822519u;
    signature.y=(signature.y<<13)|(signature.y>>19);
    return signature;
}
