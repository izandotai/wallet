# Independent derivation-vector generator (pure stdlib).
# Self-validates against official BIP84/BIP86 vectors, then emits
# BIP44/BIP49 mainnet and SLIP-0010 Solana addresses for the same
# mnemonic, to serve as cross-implementation test anchors.
import hashlib, hmac

MNEMONIC = ("abandon abandon abandon abandon abandon abandon "
            "abandon abandon abandon abandon abandon about")

def pbkdf2_sha512(password, salt, iters, dklen=64):
    # this python build lacks hashlib.pbkdf2_hmac; RFC 2898 by hand
    out = b""
    block = 1
    while len(out) < dklen:
        u = hmac.new(password, salt + block.to_bytes(4, "big"),
                     hashlib.sha512).digest()
        t = int.from_bytes(u, "big")
        for _ in range(iters - 1):
            u = hmac.new(password, u, hashlib.sha512).digest()
            t ^= int.from_bytes(u, "big")
        out += t.to_bytes(64, "big")
        block += 1
    return out[:dklen]

def bip39_seed(mnemonic, passphrase=""):
    return pbkdf2_sha512(mnemonic.encode(),
                         ("mnemonic" + passphrase).encode(), 2048)

# ---- RIPEMD-160 (pure python; hashlib here has no ripemd) ----
def _rol(x, n): return ((x << n) | (x >> (32 - n))) & 0xFFFFFFFF

_RL = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
       7,4,13,1,10,6,15,3,12,0,9,5,2,14,11,8,
       3,10,14,4,9,15,8,1,2,7,0,6,13,11,5,12,
       1,9,11,10,0,8,12,4,13,3,7,15,14,5,6,2,
       4,0,5,9,7,12,2,10,14,1,3,8,11,6,15,13]
_RR = [5,14,7,0,9,2,11,4,13,6,15,8,1,10,3,12,
       6,11,3,7,0,13,5,10,14,15,8,12,4,9,1,2,
       15,5,1,3,7,14,6,9,11,8,12,2,10,0,4,13,
       8,6,4,1,3,11,15,0,5,12,2,13,9,7,10,14,
       12,15,10,4,1,5,8,7,6,2,13,14,0,3,9,11]
_SL = [11,14,15,12,5,8,7,9,11,13,14,15,6,7,9,8,
       7,6,8,13,11,9,7,15,7,12,15,9,11,7,13,12,
       11,13,6,7,14,9,13,15,14,8,13,6,5,12,7,5,
       11,12,14,15,14,15,9,8,9,14,5,6,8,6,5,12,
       9,15,5,11,6,8,13,12,5,12,13,14,11,8,5,6]
_SR = [8,9,9,11,13,15,15,5,7,7,8,11,14,14,12,6,
       9,13,15,7,12,8,9,11,7,7,12,7,6,15,13,11,
       9,7,15,11,8,6,6,14,12,13,5,14,13,13,7,5,
       15,5,8,11,14,14,6,14,6,9,12,9,12,5,15,8,
       8,5,12,9,12,5,14,6,8,13,6,5,15,13,11,11]
_KL = [0x00000000, 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xA953FD4E]
_KR = [0x50A28BE6, 0x5C4DD124, 0x6D703EF3, 0x7A6D76E9, 0x00000000]

def _rmd_f(j, x, y, z):
    if j < 16: return x ^ y ^ z
    if j < 32: return (x & y) | (~x & z)
    if j < 48: return (x | ~y) ^ z
    if j < 64: return (x & z) | (y & ~z)
    return x ^ (y | ~z)

def ripemd160(msg):
    h = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0]
    ml = len(msg)
    msg += b"\x80" + b"\x00" * ((55 - ml) % 64) + (ml * 8).to_bytes(8, "little")
    for off in range(0, len(msg), 64):
        x = [int.from_bytes(msg[off + 4 * i : off + 4 * i + 4], "little")
             for i in range(16)]
        al, bl, cl, dl, el = h
        ar, br, cr, dr, er = h
        for j in range(80):
            t = _rol((al + _rmd_f(j, bl, cl, dl) + x[_RL[j]] + _KL[j // 16])
                     & 0xFFFFFFFF, _SL[j]) + el
            al, el, dl, cl, bl = el, dl, _rol(cl, 10), bl, t & 0xFFFFFFFF
            t = _rol((ar + _rmd_f(79 - j, br, cr, dr) + x[_RR[j]]
                      + _KR[j // 16]) & 0xFFFFFFFF, _SR[j]) + er
            ar, er, dr, cr, br = er, dr, _rol(cr, 10), br, t & 0xFFFFFFFF
        t = (h[1] + cl + dr) & 0xFFFFFFFF
        h[1] = (h[2] + dl + er) & 0xFFFFFFFF
        h[2] = (h[3] + el + ar) & 0xFFFFFFFF
        h[3] = (h[4] + al + br) & 0xFFFFFFFF
        h[4] = (h[0] + bl + cr) & 0xFFFFFFFF
        h[0] = t
    return b"".join(v.to_bytes(4, "little") for v in h)

assert ripemd160(b"abc").hex() \
    == "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc", "ripemd self-check failed"

# ---- secp256k1 ----
P  = 2**256 - 2**32 - 977
N  = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
Gx = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
Gy = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8

def inv(a, m): return pow(a, m - 2, m)

def ec_add(a, b):
    if a is None: return b
    if b is None: return a
    if a[0] == b[0] and (a[1] + b[1]) % P == 0: return None
    if a == b:
        l = 3 * a[0] * a[0] * inv(2 * a[1], P) % P
    else:
        l = (b[1] - a[1]) * inv(b[0] - a[0], P) % P
    x = (l * l - a[0] - b[0]) % P
    return (x, (l * (a[0] - x) - a[1]) % P)

def ec_mul(k, pt):
    r = None
    while k:
        if k & 1: r = ec_add(r, pt)
        pt = ec_add(pt, pt)
        k >>= 1
    return r

G = (Gx, Gy)

def ser33(pt):
    return bytes([2 + (pt[1] & 1)]) + pt[0].to_bytes(32, "big")

# ---- BIP32 ----
def bip32_master(seed):
    I = hmac.new(b"Bitcoin seed", seed, hashlib.sha512).digest()
    return int.from_bytes(I[:32], "big"), I[32:]

def bip32_ckd(k, c, i):
    if i >= 0x80000000:
        data = b"\x00" + k.to_bytes(32, "big") + i.to_bytes(4, "big")
    else:
        data = ser33(ec_mul(k, G)) + i.to_bytes(4, "big")
    I = hmac.new(c, data, hashlib.sha512).digest()
    return (int.from_bytes(I[:32], "big") + k) % N, I[32:]

def bip32_path(seed, path):
    k, c = bip32_master(seed)
    for seg in path.split("/")[1:]:
        h = seg.endswith("'")
        i = int(seg.rstrip("'")) + (0x80000000 if h else 0)
        k, c = bip32_ckd(k, c, i)
    return k

# ---- address codecs ----
B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

def b58check(payload):
    d = payload + hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4]
    n = int.from_bytes(d, "big")
    out = ""
    while n:
        n, r = divmod(n, 58)
        out = B58[r] + out
    for byte in d:
        if byte == 0: out = "1" + out
        else: break
    return out

def b58(raw):
    n = int.from_bytes(raw, "big")
    out = ""
    while n:
        n, r = divmod(n, 58)
        out = B58[r] + out
    for byte in raw:
        if byte == 0: out = "1" + out
        else: break
    return out

def hash160(b):
    return ripemd160(hashlib.sha256(b).digest())

def p2pkh(pub33): return b58check(b"\x00" + hash160(pub33))

def p2sh_p2wpkh(pub33):
    redeem = b"\x00\x14" + hash160(pub33)
    return b58check(b"\x05" + hash160(redeem))

# bech32 / bech32m
CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"

def bech32_polymod(values):
    GEN = [0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3]
    chk = 1
    for v in values:
        top = chk >> 25
        chk = (chk & 0x1FFFFFF) << 5 ^ v
        for i in range(5):
            chk ^= GEN[i] if ((top >> i) & 1) else 0
    return chk

def bech32_encode(hrp, data, const):
    values = [ord(c) >> 5 for c in hrp] + [0] + [ord(c) & 31 for c in hrp] + data
    polymod = bech32_polymod(values + [0] * 6) ^ const
    chk = [(polymod >> 5 * (5 - i)) & 31 for i in range(6)]
    return hrp + "1" + "".join(CHARSET[d] for d in data + chk)

def convertbits(data, frombits, tobits):
    acc = bits = 0
    ret = []
    maxv = (1 << tobits) - 1
    for value in data:
        acc = (acc << frombits) | value
        bits += frombits
        while bits >= tobits:
            bits -= tobits
            ret.append((acc >> bits) & maxv)
    if bits: ret.append((acc << (tobits - bits)) & maxv)
    return ret

def segwit_addr(witver, prog):
    const = 1 if witver == 0 else 0x2BC830A3
    return bech32_encode("bc", [witver] + convertbits(prog, 8, 5), const)

def p2wpkh(pub33): return segwit_addr(0, hash160(pub33))

def tagged_hash(tag, msg):
    th = hashlib.sha256(tag.encode()).digest()
    return hashlib.sha256(th + th + msg).digest()

def p2tr(pub33):
    x = pub33[1:]
    # lift_x: internal key with even y
    ptx = int.from_bytes(x, "big")
    y2 = (pow(ptx, 3, P) + 7) % P
    y = pow(y2, (P + 1) // 4, P)
    if y & 1: y = P - y
    pt = (ptx, y)
    t = int.from_bytes(tagged_hash("TapTweak", x), "big")
    assert t < N
    q = ec_add(pt, ec_mul(t, G))
    return segwit_addr(1, q[0].to_bytes(32, "big"))

# ---- ed25519 (RFC 8032) ----
ED_P = 2**255 - 19
ED_L = 2**252 + 27742317777372353535851937790883648493
ED_D = -121665 * inv(121666, ED_P) % ED_P
ED_I = pow(2, (ED_P - 1) // 4, ED_P)

def ed_add(a, b):
    x1, y1, z1, t1 = a
    x2, y2, z2, t2 = b
    A = (y1 - x1) * (y2 - x2) % ED_P
    B = (y1 + x1) * (y2 + x2) % ED_P
    C = 2 * t1 * t2 * ED_D % ED_P
    D = 2 * z1 * z2 % ED_P
    E, F, Gv, H = B - A, D - C, D + C, B + A
    return (E * F % ED_P, Gv * H % ED_P, F * Gv % ED_P, E * H % ED_P)

def ed_mul(k, pt):
    r = (0, 1, 1, 0)
    while k:
        if k & 1: r = ed_add(r, pt)
        pt = ed_add(pt, pt)
        k >>= 1
    return r

ED_By = 4 * inv(5, ED_P) % ED_P
ED_Bx = 15112221349535400772501151409588531511454012693041857206046113283949847762202
ED_B = (ED_Bx, ED_By, 1, ED_Bx * ED_By % ED_P)

def ed_pub(secret32):
    h = hashlib.sha512(secret32).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    x, y, z, _ = ed_mul(a, ED_B)
    zi = inv(z, ED_P)
    x, y = x * zi % ED_P, y * zi % ED_P
    return (y | ((x & 1) << 255)).to_bytes(32, "little")

# ---- SLIP-0010 ed25519 ----
def slip10_ed25519(seed, path):
    I = hmac.new(b"ed25519 seed", seed, hashlib.sha512).digest()
    k, c = I[:32], I[32:]
    for seg in path.split("/")[1:]:
        assert seg.endswith("'"), "ed25519 is hardened-only"
        i = int(seg.rstrip("'")) + 0x80000000
        data = b"\x00" + k + i.to_bytes(4, "big")
        I = hmac.new(c, data, hashlib.sha512).digest()
        k, c = I[:32], I[32:]
    return k

seed = bip39_seed(MNEMONIC)

def btc_pub(path):
    return ser33(ec_mul(bip32_path(seed, path), G))

# self-validation against official vectors
assert p2wpkh(btc_pub("m/84'/0'/0'/0/0")) \
    == "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu", "BIP84 self-check failed"
assert p2tr(btc_pub("m/86'/0'/0'/0/0")) \
    == "bc1p5cyxnuxmeuwuvkwfem96lqzszd02n6xdcjrs20cac6yqjjwudpxqkedrcr", \
    "BIP86 self-check failed"
print("self-checks passed (BIP84 + BIP86 official vectors)")

print("BIP44  m/44'/0'/0'/0/0 :", p2pkh(btc_pub("m/44'/0'/0'/0/0")))
print("BIP44  m/44'/0'/0'/0/1 :", p2pkh(btc_pub("m/44'/0'/0'/0/1")))
print("BIP49  m/49'/0'/0'/0/0 :", p2sh_p2wpkh(btc_pub("m/49'/0'/0'/0/0")))
print("BIP49  m/49'/0'/0'/0/1 :", p2sh_p2wpkh(btc_pub("m/49'/0'/0'/0/1")))
print("BIP86  m/86'/0'/0'/0/1 :", p2tr(btc_pub("m/86'/0'/0'/0/1")))
print("SOL    m/44'/501'/0'/0':", b58(ed_pub(slip10_ed25519(seed, "m/44'/501'/0'/0'"))))
print("SOL    m/44'/501'/1'/0':", b58(ed_pub(slip10_ed25519(seed, "m/44'/501'/1'/0'"))))
