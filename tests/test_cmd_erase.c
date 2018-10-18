#include "test_intf.c"

static void erase_s20(int use_metadata, int erase_mode, int naddrs)
{
	struct nvm_ret ret;

	struct nvm_spec_rprt *rprt = NULL;	//chunk的描述信息，报告
	struct nvm_spec_rprt_descr primes[naddrs];	//chunk概要信息

	struct nvm_addr chunk_addrs[naddrs];	//一组chunk地址
	struct nvm_addr lun_addr = { .val = 0 };	//lun形式的地址表示

	ssize_t res;

	// get an abitrary（随意的） free chunk
	// 获取一个任意的空闲块 chunk，存放到chunk_addrs
	if (nvm_cmd_rprt_arbs(dev, NVM_CHUNK_STATE_FREE, naddrs, chunk_addrs)) {
		CU_FAIL("nvm_cmd_rprt_arbs");
		return;
	}

	for (int i = 0; i < naddrs; i++) {
		//旧地址表示方式，是一个联合体
		lun_addr.l.pugrp = chunk_addrs[i].l.pugrp;
		lun_addr.l.punit = chunk_addrs[i].l.punit;

		// get report for all chunks in lun
		//获取lun中chunks的报告
		if (NULL == (rprt = nvm_cmd_rprt(dev, &lun_addr, 0, NULL))) {
			CU_FAIL("nvm_cmd_rprt failed");
			return;
		}

		//将chunk报告存入数组
		primes[i] = rprt->descr[chunk_addrs[i].l.chunk];

		CU_ASSERT(primes[i].cs == NVM_CHUNK_STATE_FREE);
	}

	//将物理地址申请为虚拟块
	struct nvm_vblk *vblk = nvm_vblk_alloc(dev, chunk_addrs, naddrs);
	if (!vblk) {
		CU_FAIL("FAILED: Allocating vblk");
		return;
	}
	//返回物理块的字节数，即大小
	size_t nbytes = nvm_vblk_get_nbytes(vblk);

	//内部调用bufs->write = nvm_buf_alloc(dev, bufs->nbytes, NULL)；
	//和bufs->write_meta = nvm_buf_alloc(dev, bufs->nbytes_meta, NULL);
	//第3个参数是0，所以不调用第二个函数，如果第2个参数是0，不调用第一个，返回buf
	struct nvm_buf_set *bufs = nvm_buf_set_alloc(dev, nbytes, 0);
	if (!bufs) {
		CU_FAIL("FAILED: Allocating nvm_buf_set");
		return;
	}
	//将写区域设置为随机，将读区域设置为0
	nvm_buf_set_fill(bufs);

	if (nvm_vblk_write(vblk, bufs->write, nbytes) < 0) {
		CU_FAIL("FAILED: nvm_vblk_write");
		return;
	}

	//同上面循环，遍历信息，存储地址等
	for (int i = 0; i < naddrs; i++) {
		lun_addr.l.pugrp = chunk_addrs[i].l.pugrp;
		lun_addr.l.punit = chunk_addrs[i].l.punit;

		// get report for all chunks in lun
		//遍历所有chunk中的lun获取信息报告
		if (NULL == (rprt = nvm_cmd_rprt(dev, &lun_addr, 0, NULL))) {
			CU_FAIL("nvm_cmd_rprt failed");
			return;
		}

		//存入数组
		primes[i] = rprt->descr[chunk_addrs[i].l.chunk];

		CU_ASSERT(primes[i].cs == NVM_CHUNK_STATE_CLOSED);
	}

	//声明结构体指针数组，并分配存储空间，用以表示块描述报告
	struct nvm_spec_rprt_descr *updated = use_metadata ?
		nvm_buf_alloc(dev, naddrs * sizeof(struct nvm_spec_rprt_descr), NULL) : NULL;

	// erase the chunk
	// 擦除一组块
	res = nvm_cmd_erase(dev, chunk_addrs, naddrs, updated, erase_mode, &ret);
	if (res < 0) {
		CU_FAIL("Erase failure");
		return;
	}

	// verify the returned metadata
	//校验擦除操作返回的元数据
	if (use_metadata) {
		for (int i = 0; i < naddrs; i++) {
			CU_ASSERT(updated[i].cs == NVM_CHUNK_STATE_FREE);
			CU_ASSERT(updated[i].ct == primes[i].ct);
			CU_ASSERT(updated[i].wli >= primes[i].wli);
			CU_ASSERT(updated[i].addr == primes[i].addr);
			CU_ASSERT(updated[i].naddrs == primes[i].naddrs);
			CU_ASSERT(updated[i].wp == 0);
		}
	}

	struct nvm_spec_rprt_descr *verify;

	// verify with a fresh report
	// 校验最新的信息报告，个人觉得没什么卵用
	for (int i = 0; i < naddrs; i++) {
		lun_addr.l.pugrp = chunk_addrs[i].l.pugrp;
		lun_addr.l.punit = chunk_addrs[i].l.punit;

		// get report for all chunks in lun
		if (NULL == (rprt = nvm_cmd_rprt(dev, &lun_addr, 0, NULL))) {
			CU_FAIL("nvm_cmd_rprt failed");
			return;
		}
		verify = &rprt->descr[chunk_addrs[i].l.chunk];
		CU_ASSERT(verify->cs == NVM_CHUNK_STATE_FREE);
		CU_ASSERT(verify->ct == primes[i].ct);
		CU_ASSERT(verify->wli >= primes[i].wli);
		CU_ASSERT(verify->addr == primes[i].addr);
		CU_ASSERT(verify->naddrs == primes[i].naddrs);
		CU_ASSERT(verify->wp == 0);
	}

	nvm_buf_free(dev, updated);
	nvm_buf_free(dev, rprt);

	return;
}

void test_ERASE_VECTOR_S20_NADDRS_ONE_META(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		CU_PASS("nothing to test");
		break;

	case NVM_SPEC_VERID_20:
		erase_s20(1, NVM_CMD_VECTOR, 1);
		break;

	default:
		CU_FAIL("invalid verid");
	}
}

void test_ERASE_VECTOR_S20_NADDRS_NPUNITS_META(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		CU_PASS("nothing to test");
		break;

	case NVM_SPEC_VERID_20:
		erase_s20(1, NVM_CMD_VECTOR, geo->l.npunit);
		break;

	default:
		CU_FAIL("invalid verid");
	}
}

void test_ERASE_VECTOR_S20_NADDRS_ONE(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		CU_PASS("nothing to test");
		break;

	case NVM_SPEC_VERID_20:
		erase_s20(0, NVM_CMD_VECTOR, 1);
		break;

	default:
		CU_FAIL("invalid verid");
	}
}

void test_ERASE_VECTOR_S20_NADDRS_NPUNITS(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		CU_PASS("nothing to test");
		break;

	case NVM_SPEC_VERID_20:
		erase_s20(0, NVM_CMD_VECTOR, geo->l.npunit);
		break;

	default:
		CU_FAIL("invalid verid");
	}
}

void test_ERASE_SCALAR_S20_NADDRS_ONE(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		CU_PASS("nothing to test");
		break;

	case NVM_SPEC_VERID_20:
		erase_s20(0, NVM_CMD_SCALAR, 1);
		break;

	default:
		CU_FAIL("invalid verid");
	}
}

void test_ERASE_SCALAR_S20_NADDRS_NPUNITS(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		CU_PASS("nothing to test");
		break;

	case NVM_SPEC_VERID_20:
		erase_s20(0, NVM_CMD_SCALAR, geo->l.npunit);
		break;

	default:
		CU_FAIL("invalid verid");
	}
}

int main(int argc, char **argv)
{
	int err = 0;

	CU_pSuite pSuite = suite_create("nvm_cmd_erase_vector",
					argc, argv);
	if (!pSuite)
		goto out;

	if (!CU_add_test(pSuite, "ERASE_SCALAR_S20_NADDRS_ONE", test_ERASE_SCALAR_S20_NADDRS_ONE))
		goto out;
	if (!CU_add_test(pSuite, "ERASE_SCALAR_S20_NADDRS_NPUNITS", test_ERASE_SCALAR_S20_NADDRS_NPUNITS))
		goto out;

	if (!CU_add_test(pSuite, "ERASE_VECTOR_S20_NADDRS_ONE", test_ERASE_VECTOR_S20_NADDRS_ONE))
		goto out;
	if (!CU_add_test(pSuite, "ERASE_VECTOR_S20_NADDRS_ONE_META", test_ERASE_VECTOR_S20_NADDRS_ONE_META))
		goto out;

	if (!CU_add_test(pSuite, "ERASE_VECTOR_S20_NADDRS_NPUNITS", test_ERASE_VECTOR_S20_NADDRS_NPUNITS))
		goto out;
	if (!CU_add_test(pSuite, "ERASE_VECTOR_S20_NADDRS_NPUNITS_META", test_ERASE_VECTOR_S20_NADDRS_NPUNITS_META))
		goto out;

/*
	// Some arbitrary size in the range [1,MAX]
	if (!CU_add_test(pSuite, "ERASE_SCALAR_S20_NADDRS_ARB", test_ERASE_SCALAR_S20_NADDRS_ARB))
		goto out;

	// Max allowed according to NVMe SPEC
	if (!CU_add_test(pSuite, "ERASE_SCALAR_S20_NADDRS_MAX", test_ERASE_SCALAR_S20_NADDRS_MAX))
		goto out;

	// Larger than MAX allowed according to NVMe SPEC, negative test, should
	// fail with EINVAL
	if (!CU_add_test(pSuite, "ERASE_SCALAR_S20_NADDRS_BAD", test_ERASE_SCALAR_S20_NADDRS_BAD))
		goto out;
*/
	switch(rmode) {
	case NVM_TEST_RMODE_AUTO:
		CU_automated_run_tests();
		break;

	default:
		CU_basic_set_mode(rmode);
		CU_basic_run_tests();
		break;
	}

out:
	err = CU_get_error() || \
	      CU_get_number_of_suites_failed() || \
	      CU_get_number_of_tests_failed() || \
	      CU_get_number_of_failures();

	CU_cleanup_registry();

	return err;
}
