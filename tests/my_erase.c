#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#include <liblightnvm.h>
#include <liblightnvm_spec.h>

static int be_id = NVM_BE_ANY;
static int seed = 0;
static char nvm_dev_path[NVM_DEV_PATH_LEN] = "/dev/nvme0n1";
static struct nvm_dev *dev;
static const struct nvm_geo *geo;

static int setup(void)
{
	srand(seed);

	dev = nvm_dev_openf(nvm_dev_path, be_id);
	if (!dev) {
		return -1;
	}

	geo = nvm_dev_get_geo(dev);

	return 0;
}

static int teardown(void)
{
	geo = NULL;
	nvm_dev_close(dev);

	return 0;
}

static void erase_s20(int use_metadata, int erase_mode, int naddrs)
{
	struct nvm_ret ret;

	struct nvm_spec_rprt *rprt = NULL;
	struct nvm_spec_rprt_descr primes[naddrs];

	struct nvm_addr chunk_addrs[naddrs];
	struct nvm_addr lun_addr = { .val = 0 };

	ssize_t res;

	// get an abitrary free chunk
	if (nvm_cmd_rprt_arbs(dev, NVM_CHUNK_STATE_FREE, naddrs, chunk_addrs)) {
		perror("nvm_cmd_rprt_arbs");
		return;
	}

	for (int i = 0; i < naddrs; i++) {
		lun_addr.l.pugrp = chunk_addrs[i].l.pugrp;
		lun_addr.l.punit = chunk_addrs[i].l.punit;

		// get report for all chunks in lun
		if (NULL == (rprt = nvm_cmd_rprt(dev, &lun_addr, 0, NULL))) {
			perror("nvm_cmd_rprt failed");
			return;
		}

		primes[i] = rprt->descr[chunk_addrs[i].l.chunk];

		assert(primes[i].cs == NVM_CHUNK_STATE_FREE);
	}

	struct nvm_vblk *vblk = nvm_vblk_alloc(dev, chunk_addrs, naddrs);
	if (!vblk) {
		perror("FAILED: Allocating vblk");
		return;
	}
	size_t nbytes = nvm_vblk_get_nbytes(vblk);

	struct nvm_buf_set *bufs = nvm_buf_set_alloc(dev, nbytes, 0);
	if (!bufs) {
		perror("FAILED: Allocating nvm_buf_set");
		return;
	}
	nvm_buf_set_fill(bufs);

	if (nvm_vblk_write(vblk, bufs->write, nbytes) < 0) {
		perror("FAILED: nvm_vblk_write");
		return;
	}

	for (int i = 0; i < naddrs; i++) {
		lun_addr.l.pugrp = chunk_addrs[i].l.pugrp;
		lun_addr.l.punit = chunk_addrs[i].l.punit;

		// get report for all chunks in lun
		if (NULL == (rprt = nvm_cmd_rprt(dev, &lun_addr, 0, NULL))) {
			perror("nvm_cmd_rprt failed");
			return;
		}

		primes[i] = rprt->descr[chunk_addrs[i].l.chunk];

		assert(primes[i].cs == NVM_CHUNK_STATE_CLOSED);
	}

	struct nvm_spec_rprt_descr *updated = use_metadata ?
		nvm_buf_alloc(dev, naddrs * sizeof(struct nvm_spec_rprt_descr), NULL) : NULL;

	// erase the chunk
	res = nvm_cmd_erase(dev, chunk_addrs, naddrs, updated, erase_mode, &ret);
	if (res < 0) {
		perror("Erase failure");
		return;
	}

	// verify the returned metadata
	if (use_metadata) {
		for (int i = 0; i < naddrs; i++) {
			assert(updated[i].cs == NVM_CHUNK_STATE_FREE);
			assert(updated[i].ct == primes[i].ct);
			assert(updated[i].wli >= primes[i].wli);
			assert(updated[i].addr == primes[i].addr);
			assert(updated[i].naddrs == primes[i].naddrs);
			assert(updated[i].wp == 0);
		}
	}

	struct nvm_spec_rprt_descr *verify;

	// verify with a fresh report
	for (int i = 0; i < naddrs; i++) {
		lun_addr.l.pugrp = chunk_addrs[i].l.pugrp;
		lun_addr.l.punit = chunk_addrs[i].l.punit;

		// get report for all chunks in lun
		if (NULL == (rprt = nvm_cmd_rprt(dev, &lun_addr, 0, NULL))) {
			perror("nvm_cmd_rprt failed");
			return;
		}
		verify = &rprt->descr[chunk_addrs[i].l.chunk];
		assert(verify->cs == NVM_CHUNK_STATE_FREE);
		assert(verify->ct == primes[i].ct);
		assert(verify->wli >= primes[i].wli);
		assert(verify->addr == primes[i].addr);
		assert(verify->naddrs == primes[i].naddrs);
		assert(verify->wp == 0);
	}

	nvm_buf_free(dev, updated);
	nvm_buf_free(dev, rprt);

	return;
}

int main(int argc, char **argv)
{
	int err = 0;

	if(setup()){
		perror("setup fail\n");
		return 0;
	}

	erase_s20(1, NVM_CMD_VECTOR, 1);

	teardown();

	return err;
}
