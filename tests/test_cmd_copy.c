/**
 * Minimal test of nvm_cmd_copy
 *
 * Requires / Depends on:
 *
 *  - That the device has two free chunks
 *  - nvm_cmd_rprt_arbs
 *  - nvm_cmd_write
 *  - nvm_cmd_read
 *  - nvm_buf
 *
 * Verifies:
 *
 *  - nvm_cmd_copy can submit and complete without error
 *
 * Two chunks, a source and a destination, are selected by consulting
 * nvm_cmd_rprt_get_arbs one is written with a constructed payload, then copied
 * to the other chunk, read and buffers compared
 */
/**
 * nvm_cmd_copy的最小测试
 *
 *要求/取决于：
 *
 * - 该设备有两个空闲块
 * - nvm_cmd_rprt_arbs
 * - nvm_cmd_write
 * - nvm_cmd_read
 * - nvm_buf
 *
 *验证：
 *
 * - nvm_cmd_copy可以无错误地提交和完成
 *
 *调用nvm_cmd_rprt_get_arbs咨询选择两个块，一个源和一个目的地
 *一个用构造的有效负载写入，然后复制到其他块，读取和缓冲区进行比较
 */
#include "test_intf.c"

#define SRC 0
#define DST 1
#define NCHUNKS 2

int cmd_copy(int cmd_opt)
{
	// 获取最佳写入扇区数
	const size_t io_nsectr = nvm_dev_get_ws_opt(dev);
	// 一个chunk中的字节数
	size_t bufs_nbytes = geo->l.nsectr * geo->l.nbytes;
	struct nvm_buf_set *bufs = NULL;
	struct nvm_addr chunks[NCHUNKS];	//N个chunk地址数组
	int res = -1;

	// 获取NCHUNKS个空闲chunk地址数组
	if (nvm_cmd_rprt_arbs(dev, NVM_CHUNK_STATE_FREE, NCHUNKS, chunks)) {
		CU_FAIL("nvm_cmd_rprt_arbs");
		goto exit;
	}

	// 申请缓冲区，大小为一个chunk的字节数
	bufs = nvm_buf_set_alloc(dev, bufs_nbytes, 0);
	if (!bufs) {
		CU_FAIL("nvm_buf_set_alloc");
		goto exit;
	}
	nvm_buf_set_fill(bufs);	// 填充缓冲区

	// 循环总长为一个chunk的sector数，步长是最佳写入扇区数
	for (size_t sectr = 0; sectr < geo->l.nsectr; sectr += io_nsectr) {
		// sec步长与sector的字节相乘，得到最佳写入字节数
		const size_t buf_ofz = sectr * geo->l.nbytes;
		struct nvm_addr src[io_nsectr];	//扇区地址数组

		// 填入地址信息，chunk号和扇区号
		for (size_t idx = 0; idx < io_nsectr; ++idx) {
			src[idx] = chunks[SRC];	// 0
			src[idx].l.sectr = sectr + idx;
		}

		// 一次写入io_nsectr个扇区的数据，数据在bufs->write中随机填写
		res = nvm_cmd_write(dev, src, io_nsectr,
				    bufs->write + buf_ofz, NULL,
				    cmd_opt, NULL);
		if (res < 0) {
			CU_FAIL("nvm_cmd_write");
			goto exit;
		}
	}

	// copy函数，将上面0号chunk的数据复制到1号chunk，内部实现没懂，感觉没实现，会返回-1，输出fail
	for (size_t sectr = 0; sectr < geo->l.nsectr; sectr += io_nsectr) {
		struct nvm_addr src[io_nsectr];
		struct nvm_addr dst[io_nsectr];

		for (size_t idx = 0; idx < io_nsectr; ++idx) {
			src[idx] = chunks[SRC];
			src[idx].l.sectr = sectr + idx;

			dst[idx] = chunks[DST];
			dst[idx].l.sectr = sectr + idx;
		}

		//这里调用了绑定的函数，具体实现没找到，不知道实现没有，只有个返回-1的函数
		res = nvm_cmd_copy(dev, src, dst, io_nsectr, 0, NULL);
		if (res < 0) {
			CU_FAIL("nvm_cmd_copy");
			goto exit;
		}
	}

	// 读取1号chunk的数据
	for (size_t sectr = 0; sectr < geo->l.nsectr; sectr += io_nsectr) {
		const size_t buf_ofz = sectr * geo->l.nbytes;
		struct nvm_addr dst[io_nsectr];

		for (size_t idx = 0; idx < io_nsectr; ++idx) {
			dst[idx] = chunks[DST];
			dst[idx].l.sectr = sectr + idx;
		}

		res = nvm_cmd_read(dev, dst, io_nsectr,
				   bufs->read + buf_ofz, NULL,
				   cmd_opt, NULL);
		if (res < 0) {
			CU_FAIL("nvm_cmd_read");
			goto exit;
		}
	}

	if (nvm_buf_diff(bufs->read, bufs->write, bufs->nbytes)) {
		CU_FAIL("buffer mismatch");
		goto exit;
	}

	CU_PASS("Success");
	res = 0;

exit:
	nvm_buf_set_free(bufs);
	return res;
}

static void test_CMD_COPY_SWR(void)
{
	CU_ASSERT(!cmd_copy(NVM_CMD_SCALAR));
}

static void test_CMD_COPY_VWR(void)
{
	CU_ASSERT(!cmd_copy(NVM_CMD_VECTOR));
}

int main(int argc, char **argv)
{
	int err = 0;

	CU_pSuite pSuite = suite_create("nvm_cmd_copy", argc, argv);
	if (!pSuite)
		goto out;

	if (!CU_ADD_TEST(pSuite, test_CMD_COPY_VWR))
		goto out;

	if (!CU_ADD_TEST(pSuite, test_CMD_COPY_SWR))
		goto out;

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
