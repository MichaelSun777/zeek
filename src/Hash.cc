// See the file "COPYING" in the main distribution directory for copyright.

#include "zeek-config.h"

#include "Hash.h"
#include "digest.h"
#include "Reporter.h"
#include "BroString.h"

#include "highwayhash/sip_hash.h"
#include "highwayhash/highwayhash_target.h"
#include "highwayhash/instruction_sets.h"

// we use the following lines to not pull in the highwayhash headers in Hash.h - but to check the types did not change underneath us.
static_assert(std::is_same<hash64_t, highwayhash::HHResult64>::value, "Highwayhash return values must match hash_x_t");
static_assert(std::is_same<hash128_t, highwayhash::HHResult128>::value, "Highwayhash return values must match hash_x_t");
static_assert(std::is_same<hash256_t, highwayhash::HHResult256>::value, "Highwayhash return values must match hash_x_t");

void KeyedHash::InitializeSeeds(const std::array<uint32_t, SEED_INIT_SIZE>& seed_data)
	{
	static_assert(std::is_same<decltype(KeyedHash::shared_siphash_key), highwayhash::SipHashState::Key>::value, "Highwayhash Key is not unsigned long long[2]");
	static_assert(std::is_same<decltype(KeyedHash::shared_highwayhash_key), highwayhash::HHKey>::value, "Highwayhash HHKey is not uint64_t[4]");
	if ( seeds_initialized )
		return;

	internal_md5((const u_char*) seed_data.data(), sizeof(seed_data) - 16, shared_hmac_md5_key); // The last 128 bits of buf are for siphash
	// yes, we use the same buffer twice to initialize two different keys. This should not really be a
	// security problem of any kind: hmac-md5 is not really used anymore - and even if it was, the hashes
	// should not reveal any information about their initialization vector.
	static_assert(sizeof(shared_highwayhash_key) == SHA256_DIGEST_LENGTH);
	calculate_digest(Hash_SHA256, (const u_char*) seed_data.data(), sizeof(seed_data) - 16, reinterpret_cast<unsigned char*>(shared_highwayhash_key));
	memcpy(shared_siphash_key, reinterpret_cast<const char*>(seed_data.data()) + 64, 16);

	seeds_initialized = true;
	}

hash64_t KeyedHash::Hash64(const void* bytes, uint64_t size)
	{
	return highwayhash::SipHash(shared_siphash_key, reinterpret_cast<const char *>(bytes), size);
	}

void KeyedHash::Hash128(const void* bytes, uint64_t size, hash128_t* result)
	{
	highwayhash::InstructionSets::Run<highwayhash::HighwayHash>(shared_highwayhash_key, reinterpret_cast<const char *>(bytes), size, result);
	}

void KeyedHash::Hash256(const void* bytes, uint64_t size, hash256_t* result)
	{
	highwayhash::InstructionSets::Run<highwayhash::HighwayHash>(shared_highwayhash_key, reinterpret_cast<const char *>(bytes), size, result);
	}

void init_hash_function()
	{
	// Make sure we have already called init_random_seed().
	if ( ! KeyedHash::IsInitialized() )
		reporter->InternalError("Zeek's hash functions aren't fully initialized");
	}

HashKey::HashKey(bro_int_t i)
	{
	key_u.i = i;
	key = (void*) &key_u;
	size = sizeof(i);
	hash = HashBytes(key, size);
	}

HashKey::HashKey(bro_uint_t u)
	{
	key_u.i = bro_int_t(u);
	key = (void*) &key_u;
	size = sizeof(u);
	hash = HashBytes(key, size);
	}

HashKey::HashKey(uint32_t u)
	{
	key_u.u32 = u;
	key = (void*) &key_u;
	size = sizeof(u);
	hash = HashBytes(key, size);
	}

HashKey::HashKey(const uint32_t u[], int n)
	{
	size = n * sizeof(u[0]);
	key = (void*) u;
	hash = HashBytes(key, size);
	}

HashKey::HashKey(double d)
	{
	union {
		double d;
		int i[2];
	} u;

	key_u.d = u.d = d;
	key = (void*) &key_u;
	size = sizeof(d);
	hash = HashBytes(key, size);
	}

HashKey::HashKey(const void* p)
	{
	key_u.p = p;
	key = (void*) &key_u;
	size = sizeof(p);
	hash = HashBytes(key, size);
	}

HashKey::HashKey(const char* s)
	{
	size = strlen(s);	// note - skip final \0
	key = (void*) s;
	hash = HashBytes(key, size);
	}

HashKey::HashKey(const BroString* s)
	{
	size = s->Len();
	key = (void*) s->Bytes();
	hash = HashBytes(key, size);
	}

HashKey::HashKey(int copy_key, void* arg_key, int arg_size)
	{
	size = arg_size;
	is_our_dynamic = true;

	if ( copy_key )
		{
		key = (void*) new char[size];
		memcpy(key, arg_key, size);
		}
	else
		key = arg_key;

	hash = HashBytes(key, size);
	}

HashKey::HashKey(const void* arg_key, int arg_size, hash_t arg_hash)
	{
	size = arg_size;
	hash = arg_hash;
	key = CopyKey(arg_key, size);
	is_our_dynamic = true;
	}

HashKey::HashKey(const void* arg_key, int arg_size, hash_t arg_hash,
		bool /* dont_copy */)
	{
	size = arg_size;
	hash = arg_hash;
	key = const_cast<void*>(arg_key);
	}

HashKey::HashKey(const void* bytes, int arg_size)
	{
	size = arg_size;
	key = CopyKey(bytes, size);
	hash = HashBytes(key, size);
	is_our_dynamic = true;
	}

void* HashKey::TakeKey()
	{
	if ( is_our_dynamic )
		{
		is_our_dynamic = false;
		return key;
		}
	else
		return CopyKey(key, size);
	}

void* HashKey::CopyKey(const void* k, int s) const
	{
	void* k_copy = (void*) new char[s];
	memcpy(k_copy, k, s);
	return k_copy;
	}

hash_t HashKey::HashBytes(const void* bytes, int size)
	{
	return KeyedHash::Hash64(bytes, size);
	}
