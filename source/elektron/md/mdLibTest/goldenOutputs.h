#pragma once

// Reference output of mdGoldenOutputTest. Generated: run mdGoldenOutputTest --update and
// replace this file, and say in the commit message why the emulated output changed.
// Hashes only; no firmware or audio data.

#include <cstdint>

namespace mdGolden
{
	struct Entry
	{
		const char* model;
		const char* scenario;
		uint64_t audio;
		uint64_t midi;
		uint64_t lcd;
	};

	inline constexpr Entry g_entries[] =
	{
		{ "MM", "idle", 0xb18910aa787e8383ull, 0x975f1fc495faa255ull, 0x999e36f297cf7dceull },	// 0 non-zero samples, 1037 MIDI events
		{ "MM", "machine 000 GND-GND", 0xb18910aa787e8383ull, 0x5c87d27b22e412bfull, 0xb0da47352e162ea9ull },	// 0 non-zero samples, 77 MIDI events
		{ "MM", "machine 001 GND-SIN", 0xb5fd497bc585aeabull, 0xaf91217e287db927ull, 0x999e36f297cf7dceull },	// 87902 non-zero samples, 76 MIDI events
		{ "MM", "machine 002 GND-NOIS", 0x3a15d6d458205ab4ull, 0x908d2b14b39a7e18ull, 0x9cd900ae79fba7feull },	// 88064 non-zero samples, 77 MIDI events
		{ "MM", "machine 004 SWAVE-SAW", 0x6ea9ae4c2a141773ull, 0x5879a60d8af3478aull, 0xc188a332db270442ull },	// 88064 non-zero samples, 77 MIDI events
		{ "MM", "machine 005 SWAVE-PULS", 0xe33e8c0b406815e5ull, 0xec692687ab3e4247ull, 0x03a96d1f2de52bd0ull },	// 88064 non-zero samples, 77 MIDI events
		{ "MM", "machine 014 SWAVE-ENS", 0x6c5e93de174e4155ull, 0xa4636def4472854aull, 0x7b316e59aab9e3f5ull },	// 88064 non-zero samples, 76 MIDI events
		{ "MM", "machine 003 SID-6581", 0xc4a7447ab469febfull, 0x9bbc829d7d4a0664ull, 0x742d74907b54cdc2ull },	// 88064 non-zero samples, 77 MIDI events
		{ "MM", "machine 006 DPRO-WAVE", 0x6f5f1a3dc70b1c86ull, 0xcb3f1de705197913ull, 0x5d3b429ea37d3ef0ull },	// 88064 non-zero samples, 77 MIDI events
		{ "MM", "machine 007 DPRO-BBOX", 0xe933fa80866cf375ull, 0x9d00163577706887ull, 0x4072d636ba806326ull },	// 87976 non-zero samples, 76 MIDI events
		{ "MM", "machine 032 DPRO-DDRW", 0x991add2995b67297ull, 0x25fe8a659716c69dull, 0x1528a39ee3dff4ecull },	// 88056 non-zero samples, 77 MIDI events
		{ "MM", "machine 033 DPRO-DENS", 0x26953cc2808638f2ull, 0x3d31b4d344b6101aull, 0x72629bb9fb93af74ull },	// 88063 non-zero samples, 77 MIDI events
		{ "MM", "machine 008 FM+-STAT", 0x007e51f0dfba0016ull, 0x3fb436ea5381446aull, 0xecfb086866548cdfull },	// 88064 non-zero samples, 77 MIDI events
		{ "MM", "machine 009 FM+-PAR", 0x2f9e0f806d343ed4ull, 0x0d8bba2e0c55eec1ull, 0xc26c98f504ae2498ull },	// 88064 non-zero samples, 76 MIDI events
		{ "MM", "machine 010 FM+-DYN", 0x5f1f77635e8088d9ull, 0x36e4e939c6825c12ull, 0x89f8f3b221f7f764ull },	// 88064 non-zero samples, 77 MIDI events
		{ "MM", "machine 011 VO-VO-6", 0x4cae18bc51cf9859ull, 0x99bd341699b8ec81ull, 0xa170e24e20604f35ull },	// 88064 non-zero samples, 77 MIDI events
		{ "MM", "machine 012 FX-THRU", 0xe6c8e31911524cd2ull, 0xee563de1a68a9ff9ull, 0x15e99557afc2eba4ull },	// 87598 non-zero samples, 77 MIDI events
		{ "MM", "machine 013 FX-REVERB", 0x6f17e990c19a998eull, 0x9e3aae0bfd50d5baull, 0x54f3a45d57299c24ull },	// 81365 non-zero samples, 76 MIDI events
		{ "MM", "machine 015 FX-CHORUS", 0x958423ef9560accbull, 0x091c7bd5ff742f56ull, 0x8bf2a054c46c2440ull },	// 46157 non-zero samples, 77 MIDI events
		{ "MM", "machine 016 FX-DYNAMIX", 0xc077cc72498179ffull, 0x843946e2b80b6c23ull, 0x7e1b17c7788ad80cull },	// 43972 non-zero samples, 77 MIDI events
		{ "MM", "machine 017 FX-RINGMOD", 0x85d6d4585ad5d9eeull, 0x7136618464601b5cull, 0x0f044524c3f33f21ull },	// 3542 non-zero samples, 76 MIDI events
		{ "MM", "sequencer pattern", 0xa06684c65b1e617eull, 0xa02dc7c88e5784aeull, 0x008bddbfab11a4b8ull },	// 352255 non-zero samples, 477 MIDI events
		{ "MM", "restored pattern", 0xb003811b2910471bull, 0xd4d1a96c4164e6f6ull, 0x008bddbfab11a4b8ull },	// 352254 non-zero samples, 1231 MIDI events
		{ "MM", "screen navigation", 0xaedac3154f8a0383ull, 0xef374f3855dfb2beull, 0xc42ba2e7c23b4aaeull },	// 0 non-zero samples, 335 MIDI events
		{ "MD", "idle", 0xb18910aa787e8383ull, 0x1aa8cdb4437d3ce3ull, 0xced06548dea6d04aull },	// 0 non-zero samples, 16 MIDI events
		{ "MD", "machine 000 GND---", 0xfc94017490eac5bbull, 0x1659833ff3d44398ull, 0x949e9a7a8aafc061ull },	// 87515 non-zero samples, 479 MIDI events
		{ "MD", "machine 001 GND-SN", 0x2cc46b91bfd2b08dull, 0x14650fb0739d0383ull, 0xb916babfc029da3bull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 002 GND-NS", 0xdcd36ec3093e28ebull, 0x14650fb0739d0383ull, 0x00a00cb8c25db0d1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 003 GND-IM", 0xff48e8f860580d72ull, 0x14650fb0739d0383ull, 0x23cc74ff6a2982eaull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 016 TRX-BD", 0xb33632cf4d7e0f4bull, 0x14650fb0739d0383ull, 0x63024cdedad58585ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 017 TRX-SD", 0x5db8052c33c6bbeaull, 0x14650fb0739d0383ull, 0x925547413f85d780ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 018 TRX-XT", 0xa9072a2b15882e86ull, 0x14650fb0739d0383ull, 0xfe1d6ce354ffb5b4ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 019 TRX-CP", 0x4c9b6bc6033f3554ull, 0x14650fb0739d0383ull, 0xfb032f1c1a907beaull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 020 TRX-RS", 0xb6e2ccd7e2b2a0bbull, 0x14650fb0739d0383ull, 0x9f2d08f81de81ff6ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 021 TRX-CB", 0xa6eea07b7997a052ull, 0x14650fb0739d0383ull, 0x03c982921b36f636ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 022 TRX-CH", 0x65f135ec2688588cull, 0x14650fb0739d0383ull, 0x9a1071eb808d2e58ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 023 TRX-OH", 0xb5335844458a6f8dull, 0x14650fb0739d0383ull, 0xb29f1fc4159f52c0ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 024 TRX-CY", 0x217292d581305e7aull, 0x14650fb0739d0383ull, 0xfd210471f1b55eb7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 025 TRX-MA", 0xfd73f9a4d0559fc1ull, 0x14650fb0739d0383ull, 0xb40be60c51f168a7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 026 TRX-CL", 0xfea5df151017cb93ull, 0x14650fb0739d0383ull, 0x5511e169560f45f6ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 027 TRX-XC", 0xaa1059ba5da846d8ull, 0x14650fb0739d0383ull, 0x579c8f1f250b709aull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 028 TRX-B2", 0xe083322424662cb2ull, 0x14650fb0739d0383ull, 0x76a0b62f1904a696ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 029 TRX-S2", 0xe393bc5a1ba02370ull, 0x14650fb0739d0383ull, 0x949e9a7a8aafc061ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 032 EFM-BD", 0xcf7b0903d701bca7ull, 0x14650fb0739d0383ull, 0x61b7bb56af4dc913ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 033 EFM-SD", 0xc4e10b5986fede47ull, 0x14650fb0739d0383ull, 0x225b4d33b0c21ac3ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 034 EFM-XT", 0x967cbb360c37955dull, 0x14650fb0739d0383ull, 0x5f274762e11b94cdull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 035 EFM-CP", 0x09f8ca1c03baa9ffull, 0x14650fb0739d0383ull, 0x9ee49c1d63dd1eb1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 036 EFM-RS", 0xc458caefe5f7513dull, 0x14650fb0739d0383ull, 0xf3696bfb8476a531ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 037 EFM-CB", 0x8e02550260c28274ull, 0x14650fb0739d0383ull, 0xd931322feec93f50ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 038 EFM-HH", 0xdce4ac65564ee679ull, 0x14650fb0739d0383ull, 0x67f2ebd0c6407d97ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 039 EFM-CY", 0x309254984f9db479ull, 0x14650fb0739d0383ull, 0x47cea59fbb8723e1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 048 E12-BD", 0xf428fd41d3ce5d07ull, 0x14650fb0739d0383ull, 0xe756f93926165167ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 049 E12-SD", 0x46bfdfa6f3c77ff7ull, 0x14650fb0739d0383ull, 0x10031da107d22d11ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 050 E12-HT", 0x81d502f4dccb273cull, 0x14650fb0739d0383ull, 0x63ceebd7cd120625ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 051 E12-LT", 0xf592ada279b25072ull, 0x14650fb0739d0383ull, 0xf96fe7447ce5a6b3ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 052 E12-CP", 0x434141a3df611e95ull, 0x14650fb0739d0383ull, 0x1222397094a6d4b4ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 053 E12-RS", 0xc9e0d4cee29a2739ull, 0x14650fb0739d0383ull, 0x0ba268634f378eabull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 054 E12-CB", 0xf0edb05e371177cfull, 0x14650fb0739d0383ull, 0x1121fcdf3d593e81ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 055 E12-CH", 0x2177fcaf8a3ddc7eull, 0x14650fb0739d0383ull, 0xecaf83c452da5e04ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 056 E12-OH", 0xfcaa267f9a1391c8ull, 0x14650fb0739d0383ull, 0xf3853f3761d3a385ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 057 E12-RC", 0x7dcfe4c747a0dcecull, 0x14650fb0739d0383ull, 0x0682876b9f288ac7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 058 E12-CC", 0x1d481bbd10124437ull, 0x14650fb0739d0383ull, 0x10a8dab97adaa2a3ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 059 E12-BR", 0xdf7a11f769099772ull, 0x14650fb0739d0383ull, 0x1bb56d6700a7ed2dull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 060 E12-TA", 0xa939532c84cfe7efull, 0x14650fb0739d0383ull, 0xf0fe0252da89c0afull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 061 E12-TR", 0xd4fee86d386fb957ull, 0x14650fb0739d0383ull, 0x059335bd0952e637ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 062 E12-SH", 0xc6b30e1181ef9920ull, 0x14650fb0739d0383ull, 0xbf0a7a83608f8ce8ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 063 E12-BC", 0x73664169c98e5390ull, 0x14650fb0739d0383ull, 0xc50a1c985c431071ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 064 P-I-BD", 0xdf76776d9a4fe67full, 0x14650fb0739d0383ull, 0x495e4ad657dcf44full },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 065 P-I-SD", 0xca4338e68bab2960ull, 0x14650fb0739d0383ull, 0x013d57bb4ec74239ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 066 P-I-MT", 0xba81ed49571c5fe2ull, 0x14650fb0739d0383ull, 0x8a2e7550c5d716d4ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 067 P-I-ML", 0xd78426d225bc11faull, 0x14650fb0739d0383ull, 0x74c3d8ff9e029cf1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 068 P-I-MA", 0xeb6827ce0768072full, 0x14650fb0739d0383ull, 0x480d279d69d71756ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 069 P-I-RS", 0xdeff57d446256712ull, 0x14650fb0739d0383ull, 0x22ff2933522ab847ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 070 P-I-RC", 0xe791b57010d9e7b2ull, 0x14650fb0739d0383ull, 0x05a0ace9e58a0e49ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 071 P-I-CC", 0x081efc967ca8c21cull, 0x14650fb0739d0383ull, 0xff7955b5c692a331ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 072 P-I-HH", 0x2b27107f2bc42de6ull, 0x14650fb0739d0383ull, 0x0e156a068b158ca9ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 080 INP-GA", 0xf9ca32d7e0352f5full, 0x14650fb0739d0383ull, 0xf58cb7a2d330db52ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 081 INP-GB", 0x0fe46802f9f8b8b5ull, 0x14650fb0739d0383ull, 0xe315a6e774b93e84ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 082 INP-FA", 0xcefdbf8fc4fea3f5ull, 0x14650fb0739d0383ull, 0xb8c72903db5e25dcull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 083 INP-FB", 0xbe8a671ffe1b73b3ull, 0x14650fb0739d0383ull, 0x5d5c52d80a8fd642ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 084 INP-EA", 0xc950abd1b06dcfd1ull, 0x14650fb0739d0383ull, 0x40f43e823b3dd32bull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 085 INP-EB", 0x569854b1cb84fd6cull, 0x14650fb0739d0383ull, 0xbe027856e8a11721ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 096", 0xfb57c5c5e68cd59full, 0x89e65c39d015b497ull, 0x43748b2ad7fd30a7ull },	// 88064 non-zero samples, 12 MIDI events
		{ "MD", "machine 097", 0x9578db3211ed9a8bull, 0x36ade791b047379cull, 0x58d43595b9516defull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 098", 0x7557ba1cd7318f62ull, 0x82a3a62fc83bb749ull, 0x43ffce4a748e408full },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 099", 0x135974cf269edcefull, 0x88f590b41b50cc69ull, 0x37ba01cebd84cc31ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 100", 0x8ebc668cf7a69b96ull, 0xd0f4464fe921a82eull, 0xf182eabfaf2bb8e7ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 101", 0x8dd109511ac7dcedull, 0x00135c5790209bd0ull, 0x16c0b7317220ab57ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 102", 0x364facfe8594fd22ull, 0x3f232bb65f2c5668ull, 0xe3ca12340f3a58dfull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 103", 0xae45a1493216da82ull, 0x3034619025e67cf1ull, 0x55aaab88a38218b3ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 104", 0x1e03422597049d2aull, 0x5b87f6ddba588f63ull, 0x7bf6170817768643ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 105", 0x00cd4343714f4ee9ull, 0x92b9e9680a1b4682ull, 0x21402d4cc078c12full },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 106", 0xa7b127c35e621625ull, 0x5d9913c5a0d4552eull, 0x2d97931b9784dd6bull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 107", 0xb7cb624df080d4abull, 0x36677a006540c497ull, 0x0c0bfd6d1a41398bull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 108", 0xfa2314fb2e1002f6ull, 0x9894ac912dc31795ull, 0xab0b709abb5ed12bull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 109", 0x03214d638851a0dfull, 0x1a5334b83890024dull, 0xd72155f22c113225ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 110", 0xaa0d75ad1211f6b6ull, 0x11384180df72138aull, 0x317c67a836fc95f3ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 111", 0x5e2e5ba2db5f73d6ull, 0x998bd6c1413db9e8ull, 0x4938b662ae11cca3ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 112 CTR-AL", 0xf83efa32e20e8ebeull, 0xe0e63e53b81fb675ull, 0xad9cdd6ab2be8247ull },	// 88064 non-zero samples, 1 MIDI events
		{ "MD", "machine 113 CTR-8P", 0xa821dcc6979c4338ull, 0x14650fb0739d0383ull, 0x28886f121b718737ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 120 CTR-RE", 0x8fded9fc659acb6cull, 0x14650fb0739d0383ull, 0x536b450aa3251e12ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 121 CTR-GB", 0x5d7eb611d051e11eull, 0x14650fb0739d0383ull, 0x8767563749f1b6f1ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 122 CTR-EQ", 0xe34a047657095428ull, 0x14650fb0739d0383ull, 0x08e6aa3e2917fbf1ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 123 CTR-DX", 0x99889e45dec6e81bull, 0x14650fb0739d0383ull, 0x2e76c5fc9f7acbfbull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 128", 0x430e62803aaa133bull, 0x14650fb0739d0383ull, 0x35292f5cb176626dull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 129", 0xd8818d03f23ac980ull, 0x14650fb0739d0383ull, 0xc65e78ebc9d93f15ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 130", 0x2d4cd0509f01cb5full, 0x14650fb0739d0383ull, 0x06512367d525c715ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 131", 0xb91dae8a5e41d73cull, 0x14650fb0739d0383ull, 0xcd69485b710d3951ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 132", 0x33b5d05d6342af37ull, 0x14650fb0739d0383ull, 0x53098f61d4cda907ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 133", 0x77422f6ee73e3bf4ull, 0x14650fb0739d0383ull, 0x69095c0fc91a85e7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 134", 0x045006a2f25b3727ull, 0x14650fb0739d0383ull, 0xf2c50e9f02a89f4full },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 135", 0xa0414a42ba06b951ull, 0x14650fb0739d0383ull, 0xbf99b858b78c3835ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 136", 0xd3ae58e77e1fbf54ull, 0x14650fb0739d0383ull, 0xe9bf36988e7435cbull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 137", 0x82f1b895e4a42c2bull, 0x14650fb0739d0383ull, 0xcd3f319c8c8a16f7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 138", 0x82649ed1aa7fd98dull, 0x14650fb0739d0383ull, 0xd3df134169624cb9ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 139", 0xb5b27788be1966c6ull, 0x14650fb0739d0383ull, 0x7404f318b044337bull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 140", 0x1f13f2296f07e9e2ull, 0x14650fb0739d0383ull, 0x907b7d2f6770d781ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 141", 0x11f13b4215ea37b3ull, 0x14650fb0739d0383ull, 0x3882b30f153ceab1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 142", 0x0561f9eb9280767eull, 0x14650fb0739d0383ull, 0x2ec0859300d3fdc7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 143", 0x202806f7848ab593ull, 0x14650fb0739d0383ull, 0xf769bb3cbd956e57ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 144", 0x0a8889dab810e30bull, 0x14650fb0739d0383ull, 0x8ace29510bddd861ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 145", 0x9d0428cb15f9fc52ull, 0x14650fb0739d0383ull, 0x75eb60fa601d3883ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 146", 0x637622453c932808ull, 0x14650fb0739d0383ull, 0x7c12013bb418bf8bull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 147", 0x2905a2ee41d7d375ull, 0x14650fb0739d0383ull, 0x6727e38a3e642795ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 148", 0xc5b71ed9348f6aaaull, 0x14650fb0739d0383ull, 0xf30561290482aabdull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 149", 0xc0f13cea45019ad5ull, 0x14650fb0739d0383ull, 0xeb5a87923ecf21bfull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 150", 0x711179e633e57904ull, 0x14650fb0739d0383ull, 0xca263bcd99c42ae3ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 151", 0xfabf56ea03e29873ull, 0x14650fb0739d0383ull, 0xcc67828f9082495full },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 152", 0xea2d21ed607723b4ull, 0x14650fb0739d0383ull, 0xee612d1b286e7da5ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 153", 0xebd5dfe81c149c49ull, 0x14650fb0739d0383ull, 0x0c115e3e3344127full },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 154", 0x45e9a705ab7b1bf2ull, 0x14650fb0739d0383ull, 0x65a73759f546610bull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 155", 0xc455d13c86849f05ull, 0x14650fb0739d0383ull, 0xd69cc3d53dd832e5ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 156", 0x5c464fc168527a54ull, 0x14650fb0739d0383ull, 0x0ead91809eb9d5e1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 157", 0x7e09491d89072f43ull, 0x14650fb0739d0383ull, 0x91a9e4278862a481ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 158", 0xb06cbf2cf7ee41bdull, 0x14650fb0739d0383ull, 0x9b45896e22261b85ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 159", 0x623604c332f6e329ull, 0x14650fb0739d0383ull, 0x36780bab349d20d3ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 176", 0xf1382eaaded949f4ull, 0x14650fb0739d0383ull, 0x54ec12053511493dull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 177", 0x5d3fac4aafe5b31dull, 0x14650fb0739d0383ull, 0xa6a1e34b7a8c0bd7ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 178", 0x1c993e6177b0e39eull, 0x14650fb0739d0383ull, 0xf28ac4e8af8c67e5ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 179", 0x2d0519cf7bddc70eull, 0x14650fb0739d0383ull, 0x3e37df64cba256d5ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 180", 0x73c96132aeb19e76ull, 0x14650fb0739d0383ull, 0x37ad87a498a984edull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 181", 0x4773a5436bad9c4full, 0x14650fb0739d0383ull, 0x003a246f8f718ff9ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 182", 0x7301f2b4153bc7abull, 0x14650fb0739d0383ull, 0x33ea9d1c97663f89ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 183", 0x1f9fa8e9fcd181d1ull, 0x14650fb0739d0383ull, 0xed4d5bd91e03e1ebull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 184", 0x918411edf75d9d23ull, 0x14650fb0739d0383ull, 0xadfbe08bda1f43dfull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 185", 0x1e086cc843e1df46ull, 0x14650fb0739d0383ull, 0xecfb34f9e8900ba7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 186", 0x6f6534086a71b622ull, 0x14650fb0739d0383ull, 0xe320d691cbd60ee7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 187", 0xe1f9b922603f08bcull, 0x14650fb0739d0383ull, 0xbc636886b5c20ea5ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 188", 0xff0dc8a838e5d96aull, 0x14650fb0739d0383ull, 0x401b827bb80e7f57ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 189", 0xdf53b4be7478a13aull, 0x14650fb0739d0383ull, 0xfa5f9c72b1124147ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 190", 0xa7fe98dd447b18ecull, 0x14650fb0739d0383ull, 0x3e2f7def97167907ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 191", 0x447c8d3ad614fdc9ull, 0x14650fb0739d0383ull, 0x61e472ce56d4d773ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 160 RAM-R1", 0x6bf442e3c6e8f57aull, 0x14650fb0739d0383ull, 0xb927f47ef38cf348ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 161 RAM-R2", 0x6f6b59a8dfe1aea8ull, 0x14650fb0739d0383ull, 0x81e928264a9d5100ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 165 RAM-R3", 0x9e7d968516645d44ull, 0x14650fb0739d0383ull, 0xb6e81fc5bf92c7e0ull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 166 RAM-R4", 0x9db6755735339595ull, 0x14650fb0739d0383ull, 0xa4750f9827832b8eull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 162 RAM-P1", 0x755e5dbd5b0a77cbull, 0x14650fb0739d0383ull, 0x8cb651cbb4d0e7d5ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 163 RAM-P2", 0xd03457f766afc6b1ull, 0x14650fb0739d0383ull, 0xbf68af0fabc3e70dull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 167 RAM-P3", 0xe87c79cf942c85cbull, 0x14650fb0739d0383ull, 0xc111d82406c9762dull },	// 88063 non-zero samples, 0 MIDI events
		{ "MD", "machine 168 RAM-P4", 0x01c4c8e146c621c6ull, 0x14650fb0739d0383ull, 0xf30c9ac74803828full },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "sequencer pattern", 0x94236d7f38669499ull, 0x268536940bd6333dull, 0x571cef8c800ba43full },	// 352205 non-zero samples, 116 MIDI events
		{ "MD", "restored pattern", 0xb04e4ef5d6d3d617ull, 0x0f53b58342f19cd0ull, 0x571cef8c800ba43full },	// 352214 non-zero samples, 132 MIDI events
		{ "MD", "screen navigation", 0x36c767caa9606383ull, 0x6894753cc9a09b91ull, 0x1592313aba38eed1ull },	// 20480 non-zero samples, 24 MIDI events
	};
}
