static const char aes_ecb_em[] =
	"\xC3\x4C\x05\x2C\xC0\xDA\x8D\x73\x45\x1A\xFE\x5F\x03\xBE\x29\x7F"
	"\x0A\xC1\x5A\x9A\xFB\xB2\x4D\x54\xAD\x99\xE9\x87\x20\x82\x72\xE2"
	"\xA3\xD4\x3B\xFF\xA6\x5D\x0E\x80\x09\x2F\x67\xA3\x14\x85\x78\x70"
	"\x35\x5F\x69\x7E\x8B\x86\x8B\x65\xB2\x5A\x04\xE1\x8D\x78\x2A\xFA"
	"\xF3\xF6\x75\x2A\xE8\xD7\x83\x11\x38\xF0\x41\x56\x06\x31\xB1\x14"
	"\x77\xBA\x00\xED\x54\x12\xDF\xF2\x7C\x8E\xD9\x1F\x3C\x37\x61\x72"
	"\x2D\x92\xDE\x89\x35\x74\x46\x34\x12\xBD\x7D\x12\x1A\x94\x95\x2F"
	"\x96\x65\x0F\x83\x59\x12\xF5\xE7\x48\x42\x27\x27\x80\x2C\x6C\xE1"
	"\x8B\x79\xEE\xCC\x93\xA0\xEE\x5D\xFF\x30\xB4\xEA\x21\x63\x6D\xA4"
	"\xC7\x37\x31\x7F\xE0\x84\x6F\x13\x2B\x23\xC8\xC2\xA6\x72\xCE\x22"
	"\xE5\x8B\x82\xBF\xBA\x53\xC0\x04\x0D\xC6\x10\xC6\x42\x12\x11\x68"
	"\x10\xB2\x96\xAB\xB4\x05\x04\x99\x5D\xB7\x1D\xDA\x0B\x7E\x26\xFB"
;
static const char aes_ecb_dm[] =
	"\x44\x41\x6A\xC2\xD1\xF5\x3C\x58\x33\x03\x91\x7E\x6B\xE9\xEB\xE0"
	"\xE3\xFD\x51\x12\x3B\x48\xA2\xE2\xAB\x1D\xB2\x98\x94\x20\x22\x22"
	"\x87\x7B\x88\xA7\x7A\xEF\x04\xF0\x55\x46\x53\x9E\x17\x25\x9F\x53"
	"\xC7\xA7\x1C\x1B\x46\x26\x16\x02\xEB\x1E\xE4\x8F\xDA\x81\x55\xA4"
	"\x48\xE3\x1E\x9E\x25\x67\x18\xF2\x92\x29\x31\x9C\x19\xF1\x5B\xA4"
	"\xCC\x01\x68\x4B\xE9\xB2\x9E\xD0\x1E\xA7\x92\x3E\x7D\x23\x80\xAA"
	"\x87\x26\xB4\xE6\x6D\x6B\x8F\xBA\xA2\x2D\x42\x98\x1A\x5A\x40\xCC"
	"\x83\xB9\xA2\x1A\x07\x10\xFD\xB9\xC6\x03\x79\x76\x13\x77\x2E\xD6"
	"\x05\x8C\xCF\xFD\xBB\xCB\x38\x2D\x1F\x6F\x56\x58\x5D\x8A\x4A\xDE"
	"\x15\x17\x3A\x0E\xB6\x5F\x5C\xC0\x5E\x70\x4E\xFE\x61\xD9\xE3\x46"
	"\x85\xF0\x83\xAC\xC6\x76\xD9\x1E\xDD\x1A\xBF\xB4\x39\x35\x23\x7A"
	"\x42\xC8\xF0\xAB\xC5\x8E\x0B\xEA\xC3\x29\x11\xD2\xDD\x9F\xA8\xC8"
;
static const char aes_ecb_vt[] =
	"\x3A\xD7\x8E\x72\x6C\x1E\xC0\x2B\x7E\xBF\xE9\x2B\x23\xD9\xEC\x34"
	"\x45\xBC\x70\x7D\x29\xE8\x20\x4D\x88\xDF\xBA\x2F\x0B\x0C\xAD\x9B"
	"\x16\x15\x56\x83\x80\x18\xF5\x28\x05\xCD\xBD\x62\x02\x00\x2E\x3F"
	"\xF5\x56\x9B\x3A\xB6\xA6\xD1\x1E\xFD\xE1\xBF\x0A\x64\xC6\x85\x4A"
	"\x6C\xD0\x25\x13\xE8\xD4\xDC\x98\x6B\x4A\xFE\x08\x7A\x60\xBD\x0C"
	"\x42\x3D\x27\x72\xA0\xCA\x56\xDA\xAB\xB4\x8D\x21\x29\x06\x29\x87"
	"\x10\x21\xF2\xA8\xDA\x70\xEB\x22\x19\xDC\x16\x80\x44\x45\xFF\x98"
	"\xC6\x36\xE3\x5B\x40\x25\x77\xF9\x69\x74\xD8\x80\x42\x95\xEB\xB8"
	"\xDD\xC6\xBF\x79\x0C\x15\x76\x0D\x8D\x9A\xEB\x6F\x9A\x75\xFD\x4E"
	"\xC7\x09\x8C\x21\x7C\x33\x4D\x0C\x9B\xDF\x37\xEA\x13\xB0\x82\x2C"
	"\x60\xF0\xFB\x0D\x4C\x56\xA8\xD4\xEE\xFE\xC5\x26\x42\x04\x04\x2D"
	"\x73\x37\x6F\xBB\xF6\x54\xD0\x68\x6E\x0E\x84\x00\x14\x77\x10\x6B"
;
static const char aes_ecb_vk[] =
	"\x0E\xDD\x33\xD3\xC6\x21\xE5\x46\x45\x5B\xD8\xBA\x14\x18\xBE\xC8"
	"\xC0\xCC\x0C\x5D\xA5\xBD\x63\xAC\xD4\x4A\x80\x77\x4F\xAD\x52\x22"
	"\x2F\x0B\x4B\x71\xBC\x77\x85\x1B\x9C\xA5\x6D\x42\xEB\x8F\xF0\x80"
	"\x6B\x1E\x2F\xFF\xE8\xA1\x14\x00\x9D\x8F\xE2\x2F\x6D\xB5\xF8\x76"
	"\xDE\x88\x5D\xC8\x7F\x5A\x92\x59\x40\x82\xD0\x2C\xC1\xE1\xB4\x2C"
	"\xC7\x49\x19\x4F\x94\x67\x3F\x9D\xD2\xAA\x19\x32\x84\x96\x30\xC1"
	"\x0C\xEF\x64\x33\x13\x91\x29\x34\xD3\x10\x29\x7B\x90\xF5\x6E\xCC"
	"\xC4\x49\x5D\x39\xD4\xA5\x53\xB2\x25\xFB\xA0\x2A\x7B\x1B\x87\xE1"
	"\xE3\x5A\x6D\xCB\x19\xB2\x01\xA0\x1E\xBC\xFA\x8A\xA2\x2B\x57\x59"
	"\x50\x75\xC2\x40\x5B\x76\xF2\x2F\x55\x34\x88\xCA\xE4\x7C\xE9\x0B"
	"\x49\xDF\x95\xD8\x44\xA0\x14\x5A\x7D\xE0\x1C\x91\x79\x33\x02\xD3"
	"\xE7\x39\x6D\x77\x8E\x94\x0B\x84\x18\xA8\x61\x20\xE5\xF4\x21\xFE"
;