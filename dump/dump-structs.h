#pragma once
#ifndef DUMP_STRUCTS_H
#define DUMP_STRUCTS_H

#include <swtpm/tpm_ioctl.h>
#include <swtpm/tpmlib.h>

#include <tss2/tss2_tpm2_types.h>

/* Sources:
 * https://github.com/stefanberger/swtpm/blob/master/include/swtpm/tpm_ioctl.h
 * 	(from /usr/include/swtpm/tpm_ioctl.h)
 * https://github.com/stefanberger/swtpm/blob/master/src/swtpm/tpmlib.h
 * 	(from ebuild /var/db/repos/gentoo/app-crypt/swtpm/swtpm-0.8.2.ebuild unpack)
 */

#define TPM_CMD(X) \
	X(CMD_, GET_CAPABILITY, ptm_cap) \
	X(CMD_, INIT, ptm_init) \
	X(CMD_, SHUTDOWN, ptm_res) \
	X(CMD_, GET_TPMESTABLISHED, ptm_est) \
	X(CMD_, SET_LOCALITY, ptm_loc) \
	X(CMD_, HASH_START, ptm_res) \
	X(CMD_, HASH_DATA, ptm_hdata) \
	X(CMD_, HASH_END, ptm_res) \
	X(CMD_, CANCEL_TPM_CMD, ptm_res) \
	X(CMD_, STORE_VOLATILE, ptm_res) \
	X(CMD_, RESET_TPMESTABLISHED, ptm_reset_est) \
	X(CMD_, GET_STATEBLOB, ptm_getstate) \
	X(CMD_, SET_STATEBLOB, ptm_setstate) \
	X(CMD_, STOP, ptm_res) \
	X(CMD_, GET_CONFIG, ptm_getconfig) \
	X(CMD_, SET_DATAFD, ptm_res) \
	X(CMD_, SET_BUFFERSIZE, ptm_setbuffersize) \
	X(CMD_, GET_INFO, ptm_getinfo) \
	X(CMD_, LOCK_STORAGE, ptm_lockstorage)


struct tpm2_pcr_extend {
	TPMI_DH_PCR pcr;
	TPMS_AUTH_COMMAND *auth;
	TPML_DIGEST_VALUES *digests;
};

struct tpm2_pcr_read {
	uint32_t count;
    TPMS_PCR_SELECTION selection[TPM2_PCR_SELECT_MAX];
};

#define TPM_CC(X) \
    X(TPM2_CC_, PCR_Event, uint8_t) \
	X(TPM2_CC_, PCR_Extend, uint8_t) \
    X(TPM2_CC_, PCR_Read, uint8_t)

enum TPM_CC_TO_CMD {
#define X(PREFIX, CC2, TYPE) \
    CMD_ ## CC2 = PREFIX ## CC2,
    TPM_CC(X)
#undef X
};

#define TPM_CAP(X) \
	X(PTM_CAP_, INIT) \
	X(PTM_CAP_, SHUTDOWN) \
	X(PTM_CAP_, GET_TPMESTABLISHED) \
	X(PTM_CAP_, SET_LOCALITY) \
	X(PTM_CAP_, HASHING) \
	X(PTM_CAP_, CANCEL_TPM_CMD) \
	X(PTM_CAP_, STORE_VOLATILE) \
	X(PTM_CAP_, RESET_TPMESTABLISHED) \
	X(PTM_CAP_, GET_STATEBLOB) \
	X(PTM_CAP_, SET_STATEBLOB) \
	X(PTM_CAP_, STOP) \
	X(PTM_CAP_, GET_CONFIG) \
	X(PTM_CAP_, SET_DATAFD) \
	X(PTM_CAP_, SET_BUFFERSIZE) \
	X(PTM_CAP_, GET_INFO) \
	X(PTM_CAP_, SEND_COMMAND_HEADER) \
	X(PTM_CAP_, LOCK_STORAGE)

#define TPM_STATE_FLAGS(X) \
	X(PTM_STATE_FLAG_, DECRYPTED) \
	X(PTM_STATE_FLAG_, ENCRYPTED)

#define TPM_BLOB_TYPES(X) \
	X(PTM_BLOB_TYPE_, PERMANENT) \
	X(PTM_BLOB_TYPE_, VOLATILE) \
	X(PTM_BLOB_TYPE_, SAVESTATE)

#define TPM_CONFIG_FLAGS(X) \
	X(PTM_CONFIG_FLAG_, FILE_KEY) \
	X(PTM_CONFIG_FLAG_, MIGRATION_KEY)

#define SWTPM_INFO_FLAGS(X) \
	X(SWTPM_INFO_, TPMSPECIFICATION) \
	X(SWTPM_INFO_, TPMATTRIBUTES)

#define TPM2_ALGS(X) \
	X(TPM2_ALG_, ERROR) \
	X(TPM2_ALG_, RSA) \
	X(TPM2_ALG_, TDES) \
	X(TPM2_ALG_, SHA1) \
	X(TPM2_ALG_, HMAC) \
	X(TPM2_ALG_, AES) \
	X(TPM2_ALG_, MGF1) \
	X(TPM2_ALG_, KEYEDHASH) \
	X(TPM2_ALG_, XOR) \
	X(TPM2_ALG_, SHA256) \
	X(TPM2_ALG_, SHA384) \
	X(TPM2_ALG_, SHA512) \
	X(TPM2_ALG_, NULL) \
	X(TPM2_ALG_, SM3_256) \
	X(TPM2_ALG_, SM4) \
	X(TPM2_ALG_, RSASSA) \
	X(TPM2_ALG_, RSAES) \
	X(TPM2_ALG_, RSAPSS) \
	X(TPM2_ALG_, OAEP) \
	X(TPM2_ALG_, ECDSA) \
	X(TPM2_ALG_, ECDH) \
	X(TPM2_ALG_, ECDAA) \
	X(TPM2_ALG_, SM2) \
	X(TPM2_ALG_, ECSCHNORR) \
	X(TPM2_ALG_, ECMQV) \
	X(TPM2_ALG_, KDF1_SP800_56A) \
	X(TPM2_ALG_, KDF2) \
	X(TPM2_ALG_, KDF1_SP800_108) \
	X(TPM2_ALG_, ECC) \
	X(TPM2_ALG_, SYMCIPHER) \
	X(TPM2_ALG_, CAMELLIA) \
	X(TPM2_ALG_, CMAC) \
	X(TPM2_ALG_, CTR) \
	X(TPM2_ALG_, SHA3_256) \
	X(TPM2_ALG_, SHA3_384) \
	X(TPM2_ALG_, SHA3_512) \
	X(TPM2_ALG_, OFB) \
	X(TPM2_ALG_, CBC) \
	X(TPM2_ALG_, CFB) \
	X(TPM2_ALG_, ECB)

#define TPM2_HASH_ALG(X) \
	X(TPM2_, SHA1, _DIGEST_SIZE) \
	X(TPM2_, SHA256, _DIGEST_SIZE) \
	X(TPM2_, SHA384, _DIGEST_SIZE) \
	X(TPM2_, SHA512, _DIGEST_SIZE) \
	X(TPM2_, SM3_256, _DIGEST_SIZE)

/* https://raw.githubusercontent.com/tpm2-software/tpm2-tools/refs/heads/master/man/tpm2_policycommandcode.1.md */
#define TPM2_CC_UNIMPLEMENTED(X) \
    X(AC_GetCapability, 0x194) \
    X(AC_Send, 0x195) \
    X(ActivateCredential, 0x147) \
    X(Certify, 0x148) \
    X(CertifyCreation, 0x14a) \
    X(ChangeEPS, 0x124) \
    X(ChangePPS, 0x125) \
    X(Clear, 0x126) \
    X(ClearControl, 0x127) \
    X(ClockRateAdjust, 0x130) \
    X(ClockSet, 0x128) \
    X(Commit, 0x18b) \
    X(ContextLoad, 0x161) \
    X(ContextSave, 0x162) \
    X(Create, 0x153) \
    X(CreateLoaded, 0x191) \
    X(CreatePrimary, 0x131) \
    X(DictionaryAttackLockReset, 0x139) \
    X(DictionaryAttackParameters, 0x13a) \
    X(Duplicate, 0x14b) \
    X(ECC_Parameters, 0x178) \
    X(ECDH_KeyGen, 0x163) \
    X(ECDH_ZGen, 0x154) \
    X(EC_Ephemeral, 0x18e) \
    X(EncryptDecrypt, 0x164) \
    X(EncryptDecrypt2, 0x193) \
    X(EventSequenceComplete, 0x185) \
    X(EvictControl, 0x120) \
    X(FieldUpgradeData, 0x141) \
    X(FieldUpgradeStart, 0x12f) \
    X(FirmwareRead, 0x179) \
    X(FlushContext, 0x165) \
    X(GetCapability, 0x17a) \
    X(GetCommandAuditDigest, 0x133) \
    X(GetRandom, 0x17b) \
    X(GetSessionAuditDigest, 0x14d) \
    X(GetTestResult, 0x17c) \
    X(GetTime, 0x14c) \
    X(Hash, 0x17d) \
    X(HashSequenceStart, 0x186) \
    X(HierarchyChangeAuth, 0x129) \
    X(HierarchyControl, 0x121) \
    X(HMAC, 0x155) \
    X(HMAC_Start, 0x15b) \
    X(Import, 0x156) \
    X(IncrementalSelfTest, 0x142) \
    X(Load, 0x157) \
    X(LoadExternal, 0x167) \
    X(MakeCredential, 0x168) \
    X(NV_Certify, 0x184) \
    X(NV_ChangeAuth, 0x13b) \
    X(NV_DefineSpace, 0x12a) \
    X(NV_Extend, 0x136) \
    X(NV_GlobalWriteLock, 0x132) \
    X(NV_Increment, 0x134) \
    X(NV_Read, 0x14e) \
    X(NV_ReadLock, 0x14f) \
    X(NV_ReadPublic, 0x169) \
    X(NV_SetBits, 0x135) \
    X(NV_UndefineSpace, 0x122) \
    X(NV_UndefineSpaceSpecial, 0x11f) \
    X(NV_Write, 0x137) \
    X(NV_WriteLock, 0x138) \
    X(ObjectChangeAuth, 0x150) \
    X(PCR_Allocate, 0x12b) \
    X(PCR_Event, 0x13c) \
    X(PCR_Extend, 0x182) \
    X(PCR_Read, 0x17e) \
    X(PCR_Reset, 0x13d) \
    X(PCR_SetAuthPolicy, 0x12c) \
    X(PCR_SetAuthValue, 0x183) \
    X(Policy_AC_SendSelect, 0x196) \
    X(PolicyAuthorize, 0x16a) \
    X(PolicyAuthorizeNV, 0x192) \
    X(PolicyAuthValue, 0x16b) \
    X(PolicyCommandCode, 0x16c) \
    X(PolicyCounterTimer, 0x16d) \
    X(PolicyCpHash, 0x16e) \
    X(PolicyDuplicationSelect, 0x188) \
    X(PolicyGetDigest, 0x189) \
    X(PolicyLocality, 0x16f) \
    X(PolicyNameHash, 0x170) \
    X(PolicyNV, 0x149) \
    X(PolicyNvWritten, 0x18f) \
    X(PolicyOR, 0x171) \
    X(PolicyPassword, 0x18c) \
    X(PolicyPCR, 0x17f) \
    X(PolicyPhysicalPresence, 0x187) \
    X(PolicyRestart, 0x180) \
    X(PolicySecret, 0x151) \
    X(PolicySigned, 0x160) \
    X(PolicyTemplate, 0x190) \
    X(PolicyTicket, 0x172) \
    X(PP_Commands, 0x12d) \
    X(Quote, 0x158) \
    X(ReadClock, 0x181) \
    X(ReadPublic, 0x173) \
    X(Rewrap, 0x152) \
    X(RSA_Decrypt, 0x159) \
    X(RSA_Encrypt, 0x174) \
    X(SelfTest, 0x143) \
    X(SequenceComplete, 0x13e) \
    X(SequenceUpdate, 0x15c) \
    X(SetAlgorithmSet, 0x13f) \
    X(SetCommandCodeAuditStatus, 0x140) \
    X(SetPrimaryPolicy, 0x12e) \
    X(Shutdown, 0x145) \
    X(Sign, 0x15d) \
    X(StartAuthSession, 0x176) \
    X(Startup, 0x144) \
    X(StirRandom, 0x146) \
    X(TestParms, 0x18a) \
    X(Unseal, 0x15e) \
    X(Vendor_TCG_Test, 0x20000000) \
    X(VerifySignature, 0x177) \
    X(ZGen_2Phase, 0x18d)

#endif // DUMP_STRUCT_SH
