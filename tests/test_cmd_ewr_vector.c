#include "test_intf.c"

#define NBYTES_QRK 4

// 比较数据是否相同的变体，多了后面两个参数，效果是部分比较，会跳过余数小的区域
size_t nvm_buf_diff_qrk(char *expected, char *actual, size_t nbytes,
			size_t nbytes_oob,
			size_t nbytes_qrk)
{
	size_t diff = 0;

	for (size_t i = 0; i < nbytes; ++i) {
		if ((i % nbytes_oob) < nbytes_qrk)
			continue;

		if (expected[i] != actual[i])
			++diff;
	}

	return diff;
}

// 1.2版本的读写，较为麻烦
void ewr_s12_1addr(int use_meta)
{
	char *buf_w = NULL, *buf_r = NULL, *meta_w = NULL, *meta_r = NULL;
	//一个lun的plane数目乘以一个page的sector数目
	const int naddrs = geo->nplanes * geo->nsectors;	
	struct nvm_addr addrs[naddrs];
	struct nvm_addr blk_addr = { .val = 0 };
	struct nvm_ret ret;
	ssize_t res;
	size_t buf_w_nbytes, meta_w_nbytes, buf_r_nbytes, meta_r_nbytes;
	int pmode = NVM_FLAG_PMODE_SNGL;
	int failed = 1;

	// 获得空闲地址blk_addr
	if (nvm_cmd_gbbt_arbs(dev, NVM_BBT_FREE, 1, &blk_addr)) {
		CU_FAIL("nvm_cmd_gbbt_arbs");
		goto out;
	}

	buf_w_nbytes = naddrs * geo->sector_nbytes;	//总字节数
	meta_w_nbytes = naddrs * geo->meta_nbytes;
	buf_r_nbytes = geo->sector_nbytes;	//每个扇区字节数
	meta_r_nbytes = geo->meta_nbytes;

	// 申请总字节数的空间，第三个参数大概是物理地址空间的要求，不确定
	buf_w = nvm_buf_alloc(dev, buf_w_nbytes, NULL);	// Setup buffers
	if (!buf_w) {
		CU_FAIL("nvm_buf_alloc");
		goto out;
	}
	nvm_buf_fill(buf_w, buf_w_nbytes);	//填上随机数

	// 申请元数据缓冲区
	meta_w = nvm_buf_alloc(dev, meta_w_nbytes, NULL);
	if (!meta_w) {
		CU_FAIL("nvm_buf_alloc");
		goto out;
	}
	// 每个字符填充字符
	for (size_t i = 0; i < meta_w_nbytes; ++i) {
		meta_w[i] = 65;
	}
	for (int i = 0; i < naddrs; ++i) {
		char meta_descr[meta_w_nbytes];
		int sec = i % geo->nsectors;	//i取余每页的sector数
		//除以每页的sector数，再取余每个lun中的plane数
		int pl = (i / geo->nsectors) % geo->nplanes;	

		//将plane号和sector号填入数组
		sprintf(meta_descr, "[P(%02d),S(%02d)]", pl, sec);
		if (strlen(meta_descr) > geo->meta_nbytes) {
			CU_FAIL("Failed constructing meta buffer");
			goto out;
		}

		memcpy(meta_w + i * geo->meta_nbytes, meta_descr,
		       strlen(meta_descr));
	}

	// 申请一个扇区的字节数
	buf_r = nvm_buf_alloc(dev, buf_r_nbytes, NULL);
	if (!buf_r) {
		CU_FAIL("nvm_buf_alloc");
		goto out;
	}

	// 申请一个OOB的字节数
	meta_r = nvm_buf_alloc(dev, meta_r_nbytes, NULL);
	if (!meta_r) {
		CU_FAIL("nvm_buf_alloc");
		goto out;
	}

	// 地址赋值，后面应该使用的是addr
	if (pmode) {
		addrs[0].ppa = blk_addr.ppa;
	} else {
		for (size_t pl = 0; pl < geo->nplanes; ++pl) {
			addrs[pl].ppa = blk_addr.ppa;

			addrs[pl].g.pl = pl;
		}
	}

	// 先擦除
	res = nvm_cmd_erase(dev, addrs, pmode ? 1 : geo->nplanes, NULL, pmode,
			    &ret);
	if (res < 0) {
		CU_FAIL("Erase failure");
		goto out;
	}

	// 外层循环geo->npages是一个block的page数量
	// 内层循环naddr是一个lun的plane数目乘以一个page的sector数目
	// 最后的结果是写满1个lun，目的可能是并行，plane可以并行，并行度为一个lun的plane数
	// 一次写命令写入naddr的扇区数目
	// plane和lun的关系应该是横竖交叉的关系，都可以并行，并非包含
	for (size_t pg = 0; pg < geo->npages; ++pg) {
		for (int i = 0; i < naddrs; ++i) {
			addrs[i].ppa = blk_addr.ppa;

			addrs[i].g.pg = pg;
			addrs[i].g.sec = i % geo->nsectors;
			addrs[i].g.pl = (i / geo->nsectors) % geo->nplanes;
		}
		res = nvm_cmd_write(dev, addrs, naddrs, buf_w,
				     use_meta ? meta_w : NULL, pmode, &ret);
		if (res < 0) {
			CU_FAIL("Write failure");
			goto out;
		}
	}

	// 读取是三层循环，plane层循环放中间也是为了并行性
	for (size_t pg = 0; pg < geo->npages; ++pg) {
		for (size_t pl = 0; pl < geo->nplanes; ++pl) {
			for (size_t sec = 0; sec < geo->nsectors; ++sec) {
				struct nvm_addr addr;
				// 地址计算，尚未仔细阅读，应当是与写入地址匹配
				size_t buf_diff = 0, meta_diff = 0;

				int bw_offset = sec * geo->sector_nbytes + \
						pl * geo->nsectors * \
						geo->sector_nbytes;
				int mw_offset = sec * geo->meta_nbytes + \
						pl * geo->nsectors * \
						geo->meta_nbytes;

				addr.ppa = blk_addr.ppa;
				addr.g.pg = pg;
				addr.g.pl = pl;
				addr.g.sec = sec;

				memset(buf_r, 0, buf_r_nbytes);
				if (use_meta)
					memset(meta_r, 0, meta_r_nbytes);

				// 读取一个sector
				res = nvm_cmd_read(dev, &addr, 1, buf_r,
						    use_meta ? meta_r : NULL, pmode, &ret);
				if (res < 0) {
					CU_FAIL("Read failure");
					goto out;
				}

				// 比较读取的内容和写入的内容
				buf_diff = nvm_buf_diff_qrk(buf_r,
							    buf_w + bw_offset,
							    buf_r_nbytes,
							    geo->g.meta_nbytes,
							    NBYTES_QRK);
				if (use_meta)
					meta_diff = nvm_buf_diff_qrk(meta_r,
							meta_w + mw_offset,
							meta_r_nbytes,
							geo->g.meta_nbytes,
							NBYTES_QRK);

				if (buf_diff)
					CU_FAIL("Read failure: buffer mismatch");
				if (use_meta && meta_diff) {
					CU_FAIL("Read failure: meta mismatch");
					if (CU_BRM_VERBOSE == rmode) {
						nvm_buf_diff_pr(meta_w + mw_offset,
								meta_r,
								meta_r_nbytes);
					}
				}
				if (buf_diff || meta_diff)
					goto out;
			}
		}
	}

	failed = 0;
	CU_PASS("Success");

out:
	if ((CU_BRM_VERBOSE == rmode) && failed) {
		printf("\n# Failed using\n");
		nvm_addr_prn(&blk_addr, 1, dev);
	}

	nvm_buf_free(dev, meta_r);
	nvm_buf_free(dev, buf_r);
	nvm_buf_free(dev, meta_w);
	nvm_buf_free(dev, buf_w);
}

void test_EWR_S12_1ADDR_META0_SNGL(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		ewr_s12_1addr(0);
		break;

	case NVM_SPEC_VERID_20:
		CU_PASS("Nothing to test");
		break;

	default:
		CU_FAIL("Invalid VERID");
	}
}

void test_EWR_S12_1ADDR_META1_SNGL(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		ewr_s12_1addr(1);
		break;

	case NVM_SPEC_VERID_20:
		CU_PASS("Nothing to test");
		break;

	default:
		CU_FAIL("Invalid VERID");
	}
}

// 带模式的1,2版本的擦除读写
void ewr_s12_naddr(int use_meta, int pmode)
{
	const int naddrs = geo->nplanes * geo->nsectors;
	struct nvm_addr blk_addr = { .val = 0 };
	struct nvm_addr addrs[naddrs];
	struct nvm_buf_set *bufs = NULL;
	struct nvm_ret ret;
	ssize_t res;
	int failed = 1;

	if (nvm_cmd_gbbt_arbs(dev, NVM_BBT_FREE, 1, &blk_addr)) {
		CU_FAIL("nvm_cmd_gbbt_arbs");
		goto out;
	}

	if (CU_BRM_VERBOSE == rmode) {
		printf("# using addr\n");
		nvm_addr_prn(&blk_addr, 1, dev);
	}

	bufs = nvm_buf_set_alloc(dev, naddrs * geo->g.sector_nbytes,
				 use_meta ? naddrs * geo->g.meta_nbytes : 0);
	if (!bufs) {
		CU_FAIL("nvm_buf_set_alloc");
		goto out;
	}
	nvm_buf_set_fill(bufs);

	// TODO: This should do quirks-testing
	if (pmode) {					///< Erase
		addrs[0].ppa = blk_addr.ppa;
		res = nvm_cmd_erase(dev, addrs, 1, NULL, pmode, &ret);
	} else {
		for (size_t pl = 0; pl < geo->nplanes; ++pl) {
			addrs[pl].ppa = blk_addr.ppa;

			addrs[pl].g.pl = pl;
		}
		res = nvm_cmd_erase(dev, addrs, geo->nplanes, NULL, pmode, &ret);
	}
	if (res < 0) {
		CU_FAIL("Erase failure");
		goto out;
	}

	for (size_t pg = 0; pg < geo->npages; ++pg) {	///< Write
		for (int i = 0; i < naddrs; ++i) {
			addrs[i].ppa = blk_addr.ppa;

			addrs[i].g.pg = pg;
			addrs[i].g.sec = i % geo->nsectors;
			addrs[i].g.pl = (i / geo->nsectors) % geo->nplanes;
		}
		res = nvm_cmd_write(dev, addrs, naddrs, bufs->write,
				    bufs->write_meta, pmode, &ret);
		if (res < 0) {
			CU_FAIL("Write failure");
			goto out;
		}
	}

	for (size_t pg = 0; pg < geo->npages; ++pg) {	///< Read
		size_t buf_diff = 0, meta_diff = 0;

		for (int i = 0; i < naddrs; ++i) {
			addrs[i].val = blk_addr.val;

			addrs[i].g.pg = pg;
			addrs[i].g.pl = (i / geo->g.nsectors) % geo->g.nplanes;
			addrs[i].g.sec = i % geo->g.nsectors;
		}

		memset(bufs->read, 0, bufs->nbytes);	///< Reset read buffers
		if (use_meta)
			memset(bufs->read_meta, 0, bufs->nbytes_meta);

		res = nvm_cmd_read(dev, addrs, naddrs, bufs->read,
				   bufs->read_meta, pmode, &ret);
		if (res < 0) {
			CU_FAIL("Read failure: command error");
			goto out;
		}

		buf_diff = nvm_buf_diff_qrk(bufs->write, bufs->read,
					    bufs->nbytes,
					    geo->g.meta_nbytes,
					    NBYTES_QRK);
		if (use_meta)
			meta_diff = nvm_buf_diff_qrk(bufs->write_meta,
						bufs->read_meta,
						bufs->nbytes_meta,
						geo->g.meta_nbytes,
						NBYTES_QRK);

		if (buf_diff)
			CU_FAIL("Read failure: buffer mismatch");
		if (use_meta && meta_diff) {
			CU_FAIL("Read failure: meta mismatch");
			if (CU_BRM_VERBOSE == rmode) {
				nvm_buf_diff_pr(bufs->write_meta,
						bufs->read_meta,
						bufs->nbytes_meta);
			}
		}
		if (buf_diff || meta_diff)
			goto out;
	}

	failed = 0;
	CU_PASS("Success");

out:
	if ((CU_BRM_VERBOSE == rmode) && failed) {
		printf("\n# Failed using\n");
		nvm_addr_prn(&blk_addr, 1, dev);
	}

	nvm_buf_set_free(bufs);

	return;
}

// 单元测试代码
void test_EWR_S12_NADDR_META0_SNGL(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		ewr_s12_naddr(0, NVM_FLAG_PMODE_SNGL);
		break;

	case NVM_SPEC_VERID_20:
		CU_PASS("Nothing to test");
		break;

	default:
		CU_FAIL("Invalid VERID");
	}
}

void test_EWR_S12_NADDR_META1_SNGL(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		ewr_s12_naddr(1, NVM_FLAG_PMODE_SNGL);
		break;

	case NVM_SPEC_VERID_20:
		CU_PASS("Nothing to test");
		break;

	default:
		CU_FAIL("Invalid VERID");
	}
}

void test_EWR_S12_NADDR_META0_DUAL(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		if (geo->nplanes >= 2) {
			ewr_s12_naddr(0, NVM_FLAG_PMODE_DUAL);
		} else {
			CU_PASS("Nothing to test");
		}
		break;

	case NVM_SPEC_VERID_20:
		CU_PASS("Nothing to test");
		break;

	default:
		CU_FAIL("Invalid VERID");
	}
}

void test_EWR_S12_NADDR_META1_DUAL(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		if (geo->nplanes >= 2) {
			ewr_s12_naddr(1, NVM_FLAG_PMODE_DUAL);
		} else {
			CU_PASS("Nothing to test");
		}
		break;

	case NVM_SPEC_VERID_20:
		CU_PASS("Nothing to test");
		break;

	default:
		CU_FAIL("Invalid VERID");
	}
}

void test_EWR_S12_NADDR_META0_QUAD(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		if (geo->nplanes >= 4) {
			ewr_s12_naddr(0, NVM_FLAG_PMODE_QUAD);
		} else {
			CU_PASS("Nothing to test");
		}
		break;

	case NVM_SPEC_VERID_20:
		CU_PASS("Nothing to test");
		break;

	default:
		CU_FAIL("Invalid VERID");
	}
}

void test_EWR_S12_NADDR_META1_QUAD(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		if (geo->nplanes >= 4) {
			ewr_s12_naddr(1, NVM_FLAG_PMODE_QUAD);
		} else {
			CU_PASS("Nothing to test");
		}
		break;

	case NVM_SPEC_VERID_20:
		CU_PASS("Nothing to test");
		break;

	default:
		CU_FAIL("Invalid VERID");
	}
}

// Erase, write, and read an single chunk
// 2.0版本的擦除 写 读单个chunk 
static void ewr_s20(int use_rwmeta, int use_erase_meta)
{
	const int naddrs = nvm_dev_get_ws_min(dev);
	struct nvm_addr addrs[naddrs];
	struct nvm_buf_set *bufs = NULL;
	struct nvm_spec_rprt_descr *erase_meta = NULL;
	struct nvm_ret ret;
	struct nvm_addr chunk_addr = { .val = 0 };
	ssize_t res;

	// 获取1块空闲块地址
	if (nvm_cmd_rprt_arbs(dev, NVM_CHUNK_STATE_FREE, 1, &chunk_addr)) {
		CU_FAIL("nvm_cmd_rprt_arbs");
		goto out;
	}

	// 申请缓冲区
	bufs = nvm_buf_set_alloc(dev, naddrs * geo->l.nbytes,
				 use_rwmeta ? naddrs * geo->l.nbytes_oob : 0);
	if (!bufs) {
		CU_FAIL("nvm_buf_set_alloc");
		goto out;
	}
	// 填充随机数据到缓冲区
	nvm_buf_set_fill(bufs);

	if (use_erase_meta) {
		erase_meta = nvm_buf_alloc(dev, naddrs * sizeof(*erase_meta), NULL);
		if (!erase_meta) {
			CU_FAIL("nvm_buf_alloc for erase_meta");
			goto out;
		}
	}

	// 擦除块
	res = nvm_cmd_erase(dev, &chunk_addr, 1, erase_meta, 0x0, &ret); ///< Erase
	if (res < 0) {
		CU_FAIL("Erase failure");
		goto out;
	}
    
	if (use_erase_meta) {
		CU_ASSERT(erase_meta->cs == NVM_CHUNK_STATE_FREE);
		CU_ASSERT(erase_meta->wp == 0);
		CU_ASSERT(erase_meta->naddrs == geo->l.nsectr);
		CU_ASSERT(erase_meta->addr == nvm_addr_gen2dev(dev, chunk_addr));
	}

								///< Write
	// 写入数据，一次是最小写入扇区
	// 外层循环是一个chunk的sector数量，跳跃长度为naddrs，最小写入扇区
	// 内层小循环得到一组扇区的地址，最小写入扇区
	for (size_t sectr = 0; sectr < geo->l.nsectr; sectr += naddrs) {
		for (int i = 0; i < naddrs; ++i) {
			addrs[i].val = chunk_addr.val;
			addrs[i].l.sectr = sectr + i;
		}
		res = nvm_cmd_write(dev, addrs, naddrs, bufs->write,
				    bufs->write_meta, 0x0, &ret);
		if (res < 0) {
			CU_FAIL("Write failure");
			goto out;
		}
	}

								///< Read
	// 写入的反过程
	for (size_t sectr = 0; sectr < geo->l.nsectr; sectr += naddrs) {
		size_t buf_diff = 0;
		size_t meta_diff = 0;

		for (int i = 0; i < naddrs; ++i) {
			addrs[i].val = chunk_addr.val;
			addrs[i].l.sectr = sectr + i;
		}

		memset(bufs->read, 0, bufs->nbytes);	///< Reset read buffers
		if (use_rwmeta)
			memset(bufs->read_meta, 0, bufs->nbytes_meta);

		res = nvm_cmd_read(dev, addrs, naddrs, bufs->read,
				   bufs->read_meta, 0x0, &ret);
		if (res < 0) {
			CU_FAIL("Read failure: command error");
			goto out;
		}

		// 内容比较，后面两个参数不懂，OOB是什么一直不懂，qrk为4
		// 但比较代码很好懂，就是跳跃比较，这个不是重点
		buf_diff = nvm_buf_diff_qrk(bufs->read, bufs->write, bufs->nbytes,
					    geo->l.nbytes_oob,
					    NBYTES_QRK);
		if (buf_diff) {
			CU_FAIL("Read failure: buffer mismatch");
		}

		if (use_rwmeta) {
			meta_diff = nvm_buf_diff_qrk(bufs->read_meta,
						 bufs->write_meta,
						 bufs->nbytes_meta,
					    geo->l.nbytes_oob,
					    NBYTES_QRK);
			if (meta_diff) {
				CU_FAIL("Read failure: meta mismatch");
				if (CU_BRM_VERBOSE == rmode) {
					nvm_buf_diff_pr(bufs->write_meta,
							bufs->read_meta,
							bufs->nbytes_meta);
				}
			}
		}

		if (buf_diff || meta_diff) {
			CU_FAIL("buf_diff || meta_diff");
			goto out;
		}
	}

	CU_PASS("Success");

out:
	nvm_buf_set_free(bufs);
	nvm_buf_free(dev, erase_meta);
}

void test_EWR_S20_RWMETA0_EMETA0(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		CU_PASS("nothing to test");
		break;

	case NVM_SPEC_VERID_20:
		ewr_s20(0, 0);
		break;

	default:
		CU_FAIL("invalid verid");
	}
}

void test_EWR_S20_RWMETA1_EMETA0(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		CU_PASS("nothing to test");
		break;

	case NVM_SPEC_VERID_20:
		ewr_s20(1, 0);
		break;

	default:
		CU_FAIL("invalid verid");
	}
}

void test_EWR_S20_RWMETA0_EMETA1(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		CU_PASS("nothing to test");
		break;

	case NVM_SPEC_VERID_20:
		ewr_s20(0, 1);
		break;

	default:
		CU_FAIL("invalid verid");
	}
}

void test_EWR_S20_RWMETA1_EMETA1(void)
{
	switch(nvm_dev_get_verid(dev)) {
	case NVM_SPEC_VERID_12:
		CU_PASS("nothing to test");
		break;

	case NVM_SPEC_VERID_20:
		ewr_s20(1, 1);
		break;

	default:
		CU_FAIL("invalid verid");
	}
}

int main(int argc, char **argv)
{
	int err = 0;

	CU_pSuite pSuite = suite_create("nvm_cmd_{erase,write,read}",
					argc, argv);
	if (!pSuite)
		goto out;

	if (!CU_add_test(pSuite, "EWR_S20_RWMETA0_EMETA0", test_EWR_S20_RWMETA0_EMETA0))
		goto out;
	if (!CU_add_test(pSuite, "EWR_S20_RWMETA1_EMETA0", test_EWR_S20_RWMETA1_EMETA0))
		goto out;
	if (!CU_add_test(pSuite, "EWR_S20_RWMETA0_EMETA1", test_EWR_S20_RWMETA0_EMETA1))
		goto out;
	if (!CU_add_test(pSuite, "EWR_S20_RWMETA1_EMETA1", test_EWR_S20_RWMETA1_EMETA1))
		goto out;

	if (!CU_add_test(pSuite, "EWR S12 - META NADDR QUAD", test_EWR_S12_NADDR_META0_QUAD))
		goto out;
	if (!CU_add_test(pSuite, "EWR S12 - META NADDR DUAL", test_EWR_S12_NADDR_META0_DUAL))
		goto out;
	if (!CU_add_test(pSuite, "EWR S12 - META NADDR SNGL", test_EWR_S12_NADDR_META0_SNGL))
		goto out;
	if (!CU_add_test(pSuite, "EWR S12 - META 1ADDR SNGL", test_EWR_S12_1ADDR_META0_SNGL))
		goto out;

	if (!CU_add_test(pSuite, "EWR S12 + META NADDR QUAD", test_EWR_S12_NADDR_META1_QUAD))
		goto out;
	if (!CU_add_test(pSuite, "EWR S12 + META NADDR DUAL", test_EWR_S12_NADDR_META1_DUAL))
		goto out;
	if (!CU_add_test(pSuite, "EWR S12 + META NADDR SNGL", test_EWR_S12_NADDR_META1_SNGL))
		goto out;
	if (!CU_add_test(pSuite, "EWR S12 + META 1ADDR SNGL", test_EWR_S12_1ADDR_META1_SNGL))
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
