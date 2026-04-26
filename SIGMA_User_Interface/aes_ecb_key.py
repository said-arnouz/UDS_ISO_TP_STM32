from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

AES_KEY = bytes([
    0x01,0x02,0x03,0x04,
    0x05,0x06,0x07,0x08,
    0x09,0x0A,0x0B,0x0C,
    0x0D,0x0E,0x0F,0x10
])

def aes_ecb_encrypt(key: bytes, data: bytes) -> bytes:
    cipher = Cipher(algorithms.AES(key), modes.ECB())
    enc = cipher.encryptor()
    return enc.update(data) + enc.finalize()

seed_input = input("SEED (hex 32 chars): ").strip().replace(" ","").upper()

if len(seed_input) != 32:
    print(f"ERROR: expected 32 hex chars, got {len(seed_input)}")
else:
    seed = bytes.fromhex(seed_input)
    key  = aes_ecb_encrypt(AES_KEY, seed)
    print("KEY :", key.hex().upper())
    