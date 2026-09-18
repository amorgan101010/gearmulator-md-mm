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
		{ "MD", "idle", 0xb18910aa787e8383ull, 0x1aa8cdb4437d3ce3ull, 0xced06548dea6d04aull },	// 0 non-zero samples, 16 MIDI events
		{ "MD", "machine 000 GND---", 0xb18910aa787e8383ull, 0x14650fb0739d0383ull, 0x949e9a7a8aafc061ull },	// 0 non-zero samples, 0 MIDI events
		{ "MD", "machine 001 GND-SN", 0xebc670c001b1c340ull, 0x14650fb0739d0383ull, 0xb916babfc029da3bull },	// 87600 non-zero samples, 0 MIDI events
		{ "MD", "machine 002 GND-NS", 0x52922548e0aa25efull, 0x14650fb0739d0383ull, 0x00a00cb8c25db0d1ull },	// 88060 non-zero samples, 0 MIDI events
		{ "MD", "machine 003 GND-IM", 0xe3c8dfa52d8caf33ull, 0x14650fb0739d0383ull, 0x23cc74ff6a2982eaull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 016 TRX-BD", 0x4fae71dcbdd386dcull, 0x14650fb0739d0383ull, 0x63024cdedad58585ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 017 TRX-SD", 0xf5bf3f389cf1e033ull, 0x14650fb0739d0383ull, 0x925547413f85d780ull },	// 88048 non-zero samples, 0 MIDI events
		{ "MD", "machine 018 TRX-XT", 0xdf5d86db63631192ull, 0x14650fb0739d0383ull, 0xfe1d6ce354ffb5b4ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 019 TRX-CP", 0x60e338f99b2a2d00ull, 0x14650fb0739d0383ull, 0xfb032f1c1a907beaull },	// 86788 non-zero samples, 0 MIDI events
		{ "MD", "machine 020 TRX-RS", 0x323ec9ecbcb4c780ull, 0x14650fb0739d0383ull, 0x9f2d08f81de81ff6ull },	// 88052 non-zero samples, 0 MIDI events
		{ "MD", "machine 021 TRX-CB", 0xb7a74f2d0d81809bull, 0x14650fb0739d0383ull, 0x03c982921b36f636ull },	// 88042 non-zero samples, 0 MIDI events
		{ "MD", "machine 022 TRX-CH", 0x4ed3885bc48db128ull, 0x14650fb0739d0383ull, 0x9a1071eb808d2e58ull },	// 87902 non-zero samples, 0 MIDI events
		{ "MD", "machine 023 TRX-OH", 0xefdde8d6bdc814aeull, 0x14650fb0739d0383ull, 0xb29f1fc4159f52c0ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 024 TRX-CY", 0x66884808d52fb704ull, 0x14650fb0739d0383ull, 0xfd210471f1b55eb7ull },	// 88062 non-zero samples, 0 MIDI events
		{ "MD", "machine 025 TRX-MA", 0xe7f74fd2815778daull, 0x14650fb0739d0383ull, 0xb40be60c51f168a7ull },	// 86966 non-zero samples, 0 MIDI events
		{ "MD", "machine 026 TRX-CL", 0x0dc9dcc5dde7ae38ull, 0x14650fb0739d0383ull, 0x5511e169560f45f6ull },	// 88038 non-zero samples, 0 MIDI events
		{ "MD", "machine 027 TRX-XC", 0x88303396cc1f5901ull, 0x14650fb0739d0383ull, 0x579c8f1f250b709aull },	// 87098 non-zero samples, 0 MIDI events
		{ "MD", "machine 028 TRX-B2", 0x93d34027f0a007ccull, 0x14650fb0739d0383ull, 0x76a0b62f1904a696ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 029 TRX-S2", 0x1931257fe6556386ull, 0x14650fb0739d0383ull, 0x949e9a7a8aafc061ull },	// 86270 non-zero samples, 0 MIDI events
		{ "MD", "machine 032 EFM-BD", 0xacaaa3d569a14c3eull, 0x14650fb0739d0383ull, 0x61b7bb56af4dc913ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 033 EFM-SD", 0x8961e4fbe82fedc3ull, 0x14650fb0739d0383ull, 0x225b4d33b0c21ac3ull },	// 88060 non-zero samples, 0 MIDI events
		{ "MD", "machine 034 EFM-XT", 0xd16c1d39cc9a8dbeull, 0x14650fb0739d0383ull, 0x5f274762e11b94cdull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 035 EFM-CP", 0xcaab2bfdc3d48b6aull, 0x14650fb0739d0383ull, 0x9ee49c1d63dd1eb1ull },	// 87392 non-zero samples, 0 MIDI events
		{ "MD", "machine 036 EFM-RS", 0x5272e21c08182fdfull, 0x14650fb0739d0383ull, 0xf3696bfb8476a531ull },	// 85561 non-zero samples, 0 MIDI events
		{ "MD", "machine 037 EFM-CB", 0x43dce600023eca95ull, 0x14650fb0739d0383ull, 0xd931322feec93f50ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 038 EFM-HH", 0x17eb7eb6ce9830d3ull, 0x14650fb0739d0383ull, 0x67f2ebd0c6407d97ull },	// 87232 non-zero samples, 0 MIDI events
		{ "MD", "machine 039 EFM-CY", 0x3ee31d9244cf59b4ull, 0x14650fb0739d0383ull, 0x47cea59fbb8723e1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 048 E12-BD", 0xa5e41a7e6ac94642ull, 0x14650fb0739d0383ull, 0xe756f93926165167ull },	// 88062 non-zero samples, 0 MIDI events
		{ "MD", "machine 049 E12-SD", 0x5ffb6a77767f5631ull, 0x14650fb0739d0383ull, 0x10031da107d22d11ull },	// 88058 non-zero samples, 0 MIDI events
		{ "MD", "machine 050 E12-HT", 0x658d8862fa764551ull, 0x14650fb0739d0383ull, 0x63ceebd7cd120625ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 051 E12-LT", 0xa4a558b194185ee0ull, 0x14650fb0739d0383ull, 0xf96fe7447ce5a6b3ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 052 E12-CP", 0xe5b0a529635330adull, 0x14650fb0739d0383ull, 0x1222397094a6d4b4ull },	// 88054 non-zero samples, 0 MIDI events
		{ "MD", "machine 053 E12-RS", 0xa0901a1d065eb88dull, 0x14650fb0739d0383ull, 0x0ba268634f378eabull },	// 85773 non-zero samples, 0 MIDI events
		{ "MD", "machine 054 E12-CB", 0x54d017f616a4ed36ull, 0x14650fb0739d0383ull, 0x1121fcdf3d593e81ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 055 E12-CH", 0x57bcbb84359c7582ull, 0x14650fb0739d0383ull, 0xecaf83c452da5e04ull },	// 87904 non-zero samples, 0 MIDI events
		{ "MD", "machine 056 E12-OH", 0x064e86f08e5d7735ull, 0x14650fb0739d0383ull, 0xf3853f3761d3a385ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 057 E12-RC", 0x0c7107ea3b60fa92ull, 0x14650fb0739d0383ull, 0x0682876b9f288ac7ull },	// 88062 non-zero samples, 0 MIDI events
		{ "MD", "machine 058 E12-CC", 0x173316372fc6199dull, 0x14650fb0739d0383ull, 0x10a8dab97adaa2a3ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 059 E12-BR", 0xc0e5e4b8197ee431ull, 0x14650fb0739d0383ull, 0x1bb56d6700a7ed2dull },	// 87150 non-zero samples, 0 MIDI events
		{ "MD", "machine 060 E12-TA", 0x59d53251bed5862aull, 0x14650fb0739d0383ull, 0xf0fe0252da89c0afull },	// 88056 non-zero samples, 0 MIDI events
		{ "MD", "machine 061 E12-TR", 0x7c1e16eb66c30166ull, 0x14650fb0739d0383ull, 0x059335bd0952e637ull },	// 88062 non-zero samples, 0 MIDI events
		{ "MD", "machine 062 E12-SH", 0xf224d46789751f9bull, 0x14650fb0739d0383ull, 0xbf0a7a83608f8ce8ull },	// 85552 non-zero samples, 0 MIDI events
		{ "MD", "machine 063 E12-BC", 0xe4a7ce1e909b3a3full, 0x14650fb0739d0383ull, 0xc50a1c985c431071ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 064 P-I-BD", 0xdcf06a953fca8d46ull, 0x14650fb0739d0383ull, 0x495e4ad657dcf44full },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 065 P-I-SD", 0x7ae364a32921a55full, 0x14650fb0739d0383ull, 0x013d57bb4ec74239ull },	// 88062 non-zero samples, 0 MIDI events
		{ "MD", "machine 066 P-I-MT", 0x65e63234e12ca89bull, 0x14650fb0739d0383ull, 0x8a2e7550c5d716d4ull },	// 88062 non-zero samples, 0 MIDI events
		{ "MD", "machine 067 P-I-ML", 0xd66a1ce8f84b72c0ull, 0x14650fb0739d0383ull, 0x74c3d8ff9e029cf1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 068 P-I-MA", 0xadcef600a3c6fec8ull, 0x14650fb0739d0383ull, 0x480d279d69d71756ull },	// 88032 non-zero samples, 0 MIDI events
		{ "MD", "machine 069 P-I-RS", 0xb4a3f868d7fe8598ull, 0x14650fb0739d0383ull, 0x22ff2933522ab847ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 070 P-I-RC", 0xb1bb377bb93a0438ull, 0x14650fb0739d0383ull, 0x05a0ace9e58a0e49ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 071 P-I-CC", 0xcb4529cb3f133de8ull, 0x14650fb0739d0383ull, 0xff7955b5c692a331ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 072 P-I-HH", 0x44c64c14568d6a6aull, 0x14650fb0739d0383ull, 0x0e156a068b158ca9ull },	// 88060 non-zero samples, 0 MIDI events
		{ "MD", "machine 080 INP-GA", 0x4820e7508be308a7ull, 0x14650fb0739d0383ull, 0xf58cb7a2d330db52ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 081 INP-GB", 0x49f8288b7df3ced0ull, 0x14650fb0739d0383ull, 0xe315a6e774b93e84ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 082 INP-FA", 0x1658bd91e07e6119ull, 0x14650fb0739d0383ull, 0xb8c72903db5e25dcull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 083 INP-FB", 0x918dd509713204f9ull, 0x14650fb0739d0383ull, 0x5d5c52d80a8fd642ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 084 INP-EA", 0xc4f2eea50e090669ull, 0x14650fb0739d0383ull, 0x40f43e823b3dd32bull },	// 85240 non-zero samples, 0 MIDI events
		{ "MD", "machine 085 INP-EB", 0x382841730854bb4dull, 0x14650fb0739d0383ull, 0xbe027856e8a11721ull },	// 86438 non-zero samples, 0 MIDI events
		{ "MD", "machine 096", 0xec2368d873140f6full, 0xe22dbae81e909ed7ull, 0x43748b2ad7fd30a7ull },	// 88064 non-zero samples, 12 MIDI events
		{ "MD", "machine 097", 0x6bf57ea7ac3a7803ull, 0x95f9b2aef52b5d09ull, 0x58d43595b9516defull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 098", 0xd1f43f5000300583ull, 0xc6f5c5be35268662ull, 0x43ffce4a748e408full },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 099", 0xb5e22f55319e4383ull, 0x3777f97792af523eull, 0x37ba01cebd84cc31ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 100", 0xb5e22f55319e4383ull, 0x91bf6d3a5bf57e09ull, 0xf182eabfaf2bb8e7ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 101", 0xb5e22f55319e4383ull, 0x69ad6f4f1a59a111ull, 0x16c0b7317220ab57ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 102", 0xb5e22f55319e4383ull, 0xbc44791390ce0094ull, 0xe3ca12340f3a58dfull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 103", 0xb5e22f55319e4383ull, 0xee88cda29d67950aull, 0x55aaab88a38218b3ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 104", 0xb5e22f55319e4383ull, 0x90272c8cf5067ee5ull, 0x7bf6170817768643ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 105", 0xb5e22f55319e4383ull, 0x5fc59ee69e7e3267ull, 0x21402d4cc078c12full },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 106", 0xb5e22f55319e4383ull, 0x2d3e84e56c091039ull, 0x2d97931b9784dd6bull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 107", 0xb5e22f55319e4383ull, 0x8600fb819854e50eull, 0x0c0bfd6d1a41398bull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 108", 0xb5e22f55319e4383ull, 0x49eded856f8c6a2dull, 0xab0b709abb5ed12bull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 109", 0xb5e22f55319e4383ull, 0x086d834206e78ff1ull, 0xd72155f22c113225ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 110", 0xb5e22f55319e4383ull, 0xb5cc505562efa3ebull, 0x317c67a836fc95f3ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 111", 0xb5e22f55319e4383ull, 0x08675750d426f845ull, 0x4938b662ae11cca3ull },	// 88064 non-zero samples, 13 MIDI events
		{ "MD", "machine 112 CTR-AL", 0xb5e22f55319e4383ull, 0xbdd0540f68cc782full, 0xad9cdd6ab2be8247ull },	// 88064 non-zero samples, 1 MIDI events
		{ "MD", "machine 113 CTR-8P", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x67c9c63b0345f96bull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 120 CTR-RE", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x536b450aa3251e12ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 121 CTR-GB", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x8767563749f1b6f1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 122 CTR-EQ", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x08e6aa3e2917fbf1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 123 CTR-DX", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x2e76c5fc9f7acbfbull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 128", 0xee6f7bb4de661777ull, 0x14650fb0739d0383ull, 0x35292f5cb176626dull },	// 85596 non-zero samples, 0 MIDI events
		{ "MD", "machine 129", 0x25755d7727c14ed4ull, 0x14650fb0739d0383ull, 0xc65e78ebc9d93f15ull },	// 88062 non-zero samples, 0 MIDI events
		{ "MD", "machine 130", 0x66ee85bcc669b9f5ull, 0x14650fb0739d0383ull, 0x06512367d525c715ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 131", 0xd4a750b6ddc2120aull, 0x14650fb0739d0383ull, 0xcd69485b710d3951ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 132", 0x129a3c2ee0a8d698ull, 0x14650fb0739d0383ull, 0x53098f61d4cda907ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 133", 0xb1c71c29794a4656ull, 0x14650fb0739d0383ull, 0x69095c0fc91a85e7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 134", 0xc501a2071a24ad7eull, 0x14650fb0739d0383ull, 0xf2c50e9f02a89f4full },	// 88062 non-zero samples, 0 MIDI events
		{ "MD", "machine 135", 0x91dda5b718d9e179ull, 0x14650fb0739d0383ull, 0xbf99b858b78c3835ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 136", 0x7fa1ba09fccb8165ull, 0x14650fb0739d0383ull, 0xe9bf36988e7435cbull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 137", 0xbbbabf0044c8cbd2ull, 0x14650fb0739d0383ull, 0xcd3f319c8c8a16f7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 138", 0x3c9d3bb6a3fadf61ull, 0x14650fb0739d0383ull, 0xd3df134169624cb9ull },	// 86186 non-zero samples, 0 MIDI events
		{ "MD", "machine 139", 0x529d638c09a41a0bull, 0x14650fb0739d0383ull, 0x7404f318b044337bull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 140", 0xd69244bee8c55f8bull, 0x14650fb0739d0383ull, 0x907b7d2f6770d781ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 141", 0x25006f04c1c197e8ull, 0x14650fb0739d0383ull, 0x3882b30f153ceab1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 142", 0xca57ea9028b84c35ull, 0x14650fb0739d0383ull, 0x2ec0859300d3fdc7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 143", 0xceeb55ec44d4417full, 0x14650fb0739d0383ull, 0xf769bb3cbd956e57ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 144", 0xe30d0aa4225ebedfull, 0x14650fb0739d0383ull, 0x8ace29510bddd861ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 145", 0x1c1b5ed9f488a312ull, 0x14650fb0739d0383ull, 0x75eb60fa601d3883ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 146", 0xd54c881df50bf85bull, 0x14650fb0739d0383ull, 0x7c12013bb418bf8bull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 147", 0x427b251b9b18da30ull, 0x14650fb0739d0383ull, 0x6727e38a3e642795ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 148", 0x00a0d610b69bf723ull, 0x14650fb0739d0383ull, 0xf30561290482aabdull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 149", 0x941144aee9e7d4a6ull, 0x14650fb0739d0383ull, 0xeb5a87923ecf21bfull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 150", 0x7ebc2534cbd7a94cull, 0x14650fb0739d0383ull, 0xca263bcd99c42ae3ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 151", 0x4625ec097578b758ull, 0x14650fb0739d0383ull, 0xcc67828f9082495full },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 152", 0x7dca0329514c1b28ull, 0x14650fb0739d0383ull, 0xee612d1b286e7da5ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 153", 0x88d81c554890d110ull, 0x14650fb0739d0383ull, 0x0c115e3e3344127full },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 154", 0xd127fa013e3333f0ull, 0x14650fb0739d0383ull, 0x65a73759f546610bull },	// 88062 non-zero samples, 0 MIDI events
		{ "MD", "machine 155", 0x7c78c97f85f99e15ull, 0x14650fb0739d0383ull, 0xd69cc3d53dd832e5ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 156", 0xdfc5e1fdb66b4cfaull, 0x14650fb0739d0383ull, 0x0ead91809eb9d5e1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 157", 0x6d29756f821719d8ull, 0x14650fb0739d0383ull, 0x91a9e4278862a481ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 158", 0x206696e7ccb00f02ull, 0x14650fb0739d0383ull, 0x9b45896e22261b85ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 159", 0xed748b6ce1a59d0bull, 0x14650fb0739d0383ull, 0x36780bab349d20d3ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 176", 0xccb6605c9d452de6ull, 0x14650fb0739d0383ull, 0x1f32030ade966a2full },	// 87568 non-zero samples, 0 MIDI events
		{ "MD", "machine 177", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x1a6e3cbb977fbd2dull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 178", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0xb3b39d18165899c7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 179", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x2e8e751d9476bcf7ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 180", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x975aff735753fd1full },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 181", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x66715739f2b48bb3ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 182", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x48831f4dd121e103ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 183", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0xcd2345995a08faf9ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 184", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0xed45b7c50fff38a5ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 185", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x9025892e6b59e87dull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 186", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0xe26a368ee1f5e2bdull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 187", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0xa6a8d3e217209087ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 188", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0xd4f44770504fa2adull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 189", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0xe7d7257e86848c1dull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 190", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x85b12134d49f1fddull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 191", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x124ea72849596ef1ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 160 RAM-R1", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0xb927f47ef38cf348ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 161 RAM-R2", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0x81e928264a9d5100ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 165 RAM-R3", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0xb6e81fc5bf92c7e0ull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 166 RAM-R4", 0xb5e22f55319e4383ull, 0x14650fb0739d0383ull, 0xa4750f9827832b8eull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 162 RAM-P1", 0xc5c908acc1889412ull, 0x14650fb0739d0383ull, 0x8cb651cbb4d0e7d5ull },	// 88054 non-zero samples, 0 MIDI events
		{ "MD", "machine 163 RAM-P2", 0x1d72f39deb4b9eabull, 0x14650fb0739d0383ull, 0xbf68af0fabc3e70dull },	// 86892 non-zero samples, 0 MIDI events
		{ "MD", "machine 167 RAM-P3", 0x90cd30716d49aa66ull, 0x14650fb0739d0383ull, 0xc111d82406c9762dull },	// 88064 non-zero samples, 0 MIDI events
		{ "MD", "machine 168 RAM-P4", 0x293571857b57a038ull, 0x14650fb0739d0383ull, 0xf30c9ac74803828full },	// 88044 non-zero samples, 0 MIDI events
	};
}
