// RCKangaroo.cpp
// 
// This file is a part of RCKangaroo software
// (c) 2024, RetiredCoder (RC)
// License: GPLv3, see "LICENSE.TXT" file
// https://github.com/RetiredC


#include <iostream>
#include <vector>

#include "cuda_runtime.h"
#include "cuda.h"

#include "defs.h"
#include "utils.h"
#include "GpuKang.h"


// Global variables and structures
EcJMP EcJumps1[JMP_CNT];
EcJMP EcJumps2[JMP_CNT];
EcJMP EcJumps3[JMP_CNT];

RCGpuKang* GpuKangs[MAX_GPU_CNT];
int GpuCnt;
volatile long ThrCnt;
volatile bool gSolved;

EcInt Int_HalfRange;
EcPoint Pnt_HalfRange;
EcPoint Pnt_NegHalfRange;
EcInt Int_TameOffset;
Ec ec;

CriticalSection csAddPoints;
u8* pPntList;
u8* pPntList2;
volatile int PntIndex;
TFastBase db;
EcPoint gPntToSolve;
EcInt gPrivKey;

volatile u64 TotalOps;
u32 TotalSolved;
u32 gTotalErrors;
u64 PntTotalOps;
bool IsBench;

u32 gDP;
u32 gRange;
EcInt gStart;
bool gStartSet;
EcPoint gPubKey;
u8 gGPUs_Mask[MAX_GPU_CNT];
char gTamesFileName[1024];
double gMax;
bool gGenMode; //tames generation mode
bool gIsOpsLimit;

#pragma pack(push, 1)
struct DBRec
{
	u8 x[12];
	u8 d[22];
	u8 type; //0 - tame, 1 - wild1, 2 - wild2
};
#pragma pack(pop)

/**
 * @brief Initializes the available GPUs.
 */
void InitGpus()
{
	GpuCnt = 0;
	int gcnt = 0;
	cudaGetDeviceCount(&gcnt);
	if (gcnt > MAX_GPU_CNT)
		gcnt = MAX_GPU_CNT;

	//	gcnt = 1; //dbg
	if (!gcnt)
		return;

	int drv, rt;
	cudaRuntimeGetVersion(&rt);
	cudaDriverGetVersion(&drv);
	char drvver[100];
	sprintf(drvver, "%d.%d/%d.%d", drv / 1000, (drv % 100) / 10, rt / 1000, (rt % 100) / 10);

	printf("CUDA devices: %d, CUDA driver/runtime: %s\r\n", gcnt, drvver);
	cudaError_t cudaStatus;
	for (int i = 0; i < gcnt; i++)
	{
		cudaStatus = cudaSetDevice(i);
		if (cudaStatus != cudaSuccess)
		{
			printf("cudaSetDevice for gpu %d failed!\r\n", i);
			continue;
		}

		if (!gGPUs_Mask[i])
			continue;

		cudaDeviceProp deviceProp;
		cudaGetDeviceProperties(&deviceProp, i);
		printf("GPU %d: %s, %.2f GB, %d CUs, cap %d.%d, PCI %d, L2 size: %d KB\r\n", i, deviceProp.name, ((float)(deviceProp.totalGlobalMem / (1024 * 1024))) / 1024.0f, deviceProp.multiProcessorCount, deviceProp.major, deviceProp.minor, deviceProp.pciBusID, deviceProp.l2CacheSize / 1024);

		if (deviceProp.major < 6)
		{
			printf("GPU %d - not supported, skip\r\n", i);
			continue;
		}

		cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync);

		GpuKangs[GpuCnt] = new RCGpuKang();
		GpuKangs[GpuCnt]->CudaIndex = i;
		GpuKangs[GpuCnt]->persistingL2CacheMaxSize = deviceProp.persistingL2CacheMaxSize;
		GpuKangs[GpuCnt]->mpCnt = deviceProp.multiProcessorCount;
		GpuKangs[GpuCnt]->IsOldGpu = deviceProp.l2CacheSize < 16 * 1024 * 1024;
		GpuCnt++;
	}
	printf("Total GPUs for work: %d\r\n", GpuCnt);
}

/**
 * @brief Thread procedure for GPU execution on Windows.
 *
 * @param data Pointer to the RCGpuKang object.
 * @return u32
 */
#ifdef _WIN32
u32 __stdcall kang_thr_proc(void* data)
{
	RCGpuKang* Kang = (RCGpuKang*)data;
	Kang->Execute();
	InterlockedDecrement(&ThrCnt);
	return 0;
}
/**
 * @brief Thread procedure for GPU execution on Linux.
 *
 * @param data Pointer to the RCGpuKang object.
 * @return void*
 */
#else
void* kang_thr_proc(void* data)
{
	RCGpuKang* Kang = (RCGpuKang*)data;
	Kang->Execute();
	__sync_fetch_and_sub(&ThrCnt, 1);
	return 0;
}
#endif

/**
 * @brief Adds points to the list and updates the operation count.
 *
 * @param data Pointer to the data.
 * @param pnt_cnt Number of points.
 * @param ops_cnt Number of operations.
 */

void AddPointsToList(u32* data, int pnt_cnt, u64 ops_cnt)
{
	csAddPoints.Enter();
	if (PntIndex + pnt_cnt >= MAX_CNT_LIST)
	{
		csAddPoints.Leave();
		printf("DPs buffer overflow, some points lost, increase DP value! \r\n");
		return;
	}
	memcpy(pPntList + GPU_DP_SIZE * PntIndex, data, pnt_cnt * GPU_DP_SIZE);
	PntIndex += pnt_cnt;
	PntTotalOps += ops_cnt;
	csAddPoints.Leave();
}

/**
 * @brief Checks for collisions using the SOTA method.
 *
 * @param pnt The point to check.
 * @param t The tame kangaroo distance.
 * @param TameType The type of the tame kangaroo.
 * @param w The wild kangaroo distance.
 * @param WildType The type of the wild kangaroo.
 * @param IsNeg Whether to negate the distance.
 * @return true If a collision is found.
 * @return false Otherwise.
 */

bool Collision_SOTA(EcPoint& pnt, EcInt t, int TameType, EcInt w, int WildType, bool IsNeg)
{
	if (IsNeg)
		t.Neg();
	if (TameType == TAME)
	{
		gPrivKey = t;
		gPrivKey.Sub(w);
		EcInt sv = gPrivKey;
		gPrivKey.Add(Int_HalfRange);
		EcPoint P = ec.MultiplyG(gPrivKey);
		if (P.IsEqual(pnt))
			return true;
		gPrivKey = sv;
		gPrivKey.Neg();
		gPrivKey.Add(Int_HalfRange);
		P = ec.MultiplyG(gPrivKey);
		return P.IsEqual(pnt);
	}
	else
	{
		gPrivKey = t;
		gPrivKey.Sub(w);
		if (gPrivKey.data[4] >> 63)
			gPrivKey.Neg();
		gPrivKey.ShiftRight(1);
		EcInt sv = gPrivKey;
		gPrivKey.Add(Int_HalfRange);
		EcPoint P = ec.MultiplyG(gPrivKey);
		if (P.IsEqual(pnt))
			return true;
		gPrivKey = sv;
		gPrivKey.Neg();
		gPrivKey.Add(Int_HalfRange);
		P = ec.MultiplyG(gPrivKey);
		return P.IsEqual(pnt);
	}
}

/**
 * @brief Checks for new points and processes them.
 */

void CheckNewPoints()
{
	csAddPoints.Enter();
	if (!PntIndex)
	{
		csAddPoints.Leave();
		return;
	}

	int cnt = PntIndex;
	memcpy(pPntList2, pPntList, GPU_DP_SIZE * cnt);
	PntIndex = 0;
	csAddPoints.Leave();

	for (int i = 0; i < cnt; i++)
	{
		DBRec nrec;
		u8* p = pPntList2 + i * GPU_DP_SIZE;
		memcpy(nrec.x, p, 12);
		memcpy(nrec.d, p + 16, 22);
		nrec.type = gGenMode ? TAME : p[40];

		DBRec* pref = (DBRec*)db.FindOrAddDataBlock((u8*)&nrec);
		if (gGenMode)
			continue;
		if (pref)
		{
			//in db we dont store first 3 bytes so restore them
			DBRec tmp_pref;
			memcpy(&tmp_pref, &nrec, 3);
			memcpy(((u8*)&tmp_pref) + 3, pref, sizeof(DBRec) - 3);
			pref = &tmp_pref;

			if (pref->type == nrec.type)
			{
				if (pref->type == TAME)
					continue;

				//if it's wild, we can find the key from the same type if distances are different
				if (*(u64*)pref->d == *(u64*)nrec.d)
					continue;
				//else
				//	ToLog("key found by same wild");
			}

			EcInt w, t;
			int TameType, WildType;
			if (pref->type != TAME)
			{
				memcpy(w.data, pref->d, sizeof(pref->d));
				if (pref->d[21] == 0xFF) memset(((u8*)w.data) + 22, 0xFF, 18);
				memcpy(t.data, nrec.d, sizeof(nrec.d));
				if (nrec.d[21] == 0xFF) memset(((u8*)t.data) + 22, 0xFF, 18);
				TameType = nrec.type;
				WildType = pref->type;
			}
			else
			{
				memcpy(w.data, nrec.d, sizeof(nrec.d));
				if (nrec.d[21] == 0xFF) memset(((u8*)w.data) + 22, 0xFF, 18);
				memcpy(t.data, pref->d, sizeof(pref->d));
				if (pref->d[21] == 0xFF) memset(((u8*)t.data) + 22, 0xFF, 18);
				TameType = TAME;
				WildType = nrec.type;
			}

			bool res = Collision_SOTA(gPntToSolve, t, TameType, w, WildType, false) || Collision_SOTA(gPntToSolve, t, TameType, w, WildType, true);
			if (!res)
			{
				bool w12 = ((pref->type == WILD1) && (nrec.type == WILD2)) || ((pref->type == WILD2) && (nrec.type == WILD1));
				if (w12) //in rare cases WILD and WILD2 can collide in mirror, in this case there is no way to find K
					;// ToLog("W1 and W2 collides in mirror");
				else
				{
					printf("Collision Error\r\n");
					gTotalErrors++;
				}
				continue;
			}
			gSolved = true;
			break;
		}
	}
}

/**
// An attempt to reduce the # of calcs performed by CheckNewPoints
void CheckNewPoints()
{
	csAddPoints.Enter();
	if (!PntIndex)
	{
		csAddPoints.Leave();
		return;
	}

	int cnt = PntIndex;
	std::swap(pPntList, pPntList2); // Swap pointers instead of copying memory
	PntIndex = 0;
	csAddPoints.Leave();

	for (int i = 0; i < cnt; i++)
	{
		DBRec nrec;
		u8* p = pPntList2 + i * GPU_DP_SIZE;
		memcpy(nrec.x, p, 12);
		memcpy(nrec.d, p + 16, 22);
		nrec.type = gGenMode ? TAME : p[40];

		DBRec* pref = (DBRec*)db.FindOrAddDataBlock((u8*)&nrec);
		if (gGenMode)
			continue;
		if (pref)
		{
			//in db we dont store first 3 bytes so restore them
			DBRec tmp_pref;
			memcpy(&tmp_pref, &nrec, 3);
			memcpy(((u8*)&tmp_pref) + 3, pref, sizeof(DBRec) - 3);
			pref = &tmp_pref;

			if (pref->type == nrec.type)
			{
				if (pref->type == TAME)
					continue;

				//if it's wild, we can find the key from the same type if distances are different
				if (*(u64*)pref->d == *(u64*)nrec.d)
					continue;
			}

			EcInt w, t;
			int TameType, WildType;
			if (pref->type != TAME)
			{
				memcpy(w.data, pref->d, sizeof(pref->d));
				if (pref->d[21] == 0xFF) memset(((u8*)w.data) + 22, 0xFF, 18);
				memcpy(t.data, nrec.d, sizeof(nrec.d));
				if (nrec.d[21] == 0xFF) memset(((u8*)t.data) + 22, 0xFF, 18);
				TameType = nrec.type;
				WildType = pref->type;
			}
			else
			{
				memcpy(w.data, nrec.d, sizeof(nrec.d));
				if (nrec.d[21] == 0xFF) memset(((u8*)w.data) + 22, 0xFF, 18);
				memcpy(t.data, pref->d, sizeof(pref->d));
				if (pref->d[21] == 0xFF) memset(((u8*)t.data) + 22, 0xFF, 18);
				TameType = TAME;
				WildType = nrec.type;
			}

			bool res = Collision_SOTA(gPntToSolve, t, TameType, w, WildType, false) || Collision_SOTA(gPntToSolve, t, TameType, w, WildType, true);
			if (!res)
			{
				bool w12 = ((pref->type == WILD1) && (nrec.type == WILD2)) || ((pref->type == WILD2) && (nrec.type == WILD1));
				if (w12) //in rare cases WILD and WILD2 can collide in mirror, in this case there is no way to find K
					;// ToLog("W1 and W2 collides in mirror");
				else
				{
					printf("Collision Error\r\n");
					gTotalErrors++;
				}
				continue;
			}
			gSolved = true;
			break;
		}
	}
}
*/


/**
 * @brief Displays the current statistics of the computation.
 *
 * @param tm_start The start time of the computation.
 * @param exp_ops The expected number of operations.
 * @param dp_val The DP value.
 */

void ShowStats(u64 tm_start, double exp_ops, double dp_val)
{
#ifdef DEBUG_MODE
	for (int i = 0; i <= MD_LEN; i++)
	{
		u64 val = 0;
		for (int j = 0; j < GpuCnt; j++)
		{
			val += GpuKangs[j]->dbg[i];
		}
		if (val)
			printf("Loop size %d: %llu\r\n", i, val);
	}
#endif

	int speed = GpuKangs[0]->GetStatsSpeed();
	for (int i = 1; i < GpuCnt; i++)
		speed += GpuKangs[i]->GetStatsSpeed();

	u64 est_dps_cnt = (u64)(exp_ops / dp_val);
	u64 exp_sec = 0xFFFFFFFFFFFFFFFFull;
	if (speed)
		exp_sec = (u64)((exp_ops / 1000000) / speed); //in sec
	u64 exp_days = exp_sec / (3600 * 24);
	u64 remaining_exp_sec = exp_sec % (3600 * 24);
	int exp_hours = static_cast<int>(remaining_exp_sec / 3600);
	remaining_exp_sec %= 3600;
	int exp_min = static_cast<int>(remaining_exp_sec / 60);
	double exp_full_sec = (remaining_exp_sec % 60) + 0.0;  // No fractional seconds for estimated

	u64 elapsed_sec = (GetTickCount64() - tm_start) / 1000;  // Total elapsed seconds
	u64 elapsed_days = elapsed_sec / (3600 * 24);
	u64 remaining_elapsed_sec = elapsed_sec % (3600 * 24);
	int elapsed_hours = static_cast<int>(remaining_elapsed_sec / 3600);
	remaining_elapsed_sec %= 3600;
	int elapsed_minutes = static_cast<int>(remaining_elapsed_sec / 60);
	double elapsed_full_sec = (remaining_elapsed_sec % 60) + ((GetTickCount64() - tm_start) % 1000) / 1000.0;

	printf("%sSpeed: %d MKeys/s, Err: %d, DPs: %lluK/%lluK, Time: %llud:%02dh:%02dm:%05.2fs/%llud:%02dh:%02dm:%05.2fs\r\n",
		gGenMode ? "GEN: " : (IsBench ? "BENCH: " : "MAIN: "),
		speed,
		gTotalErrors,
		db.GetBlockCnt() / 1000,
		est_dps_cnt / 1000,
		elapsed_days, elapsed_hours, elapsed_minutes, elapsed_full_sec,
		exp_days, exp_hours, exp_min, exp_full_sec);
}

/**
 * @brief Solves the ECDLP for a given point using the Kangaroo method.
 *
 * @param PntToSolve The point to solve.
 * @param Range The range of the search.
 * @param DP The DP value.
 * @param pk_res The resulting private key.
 * @return true If the point is solved.
 * @return false Otherwise.
 */

//bool SolvePoint(EcPoint PntToSolve, int Range, int DP, EcInt* pk_res)
//{
//	if ((Range < 19) || (Range > 160))
//	{
//		printf("Unsupported Range value (%d)!\r\n", Range);
//		return false;
//	}
//	if ((DP < 4) || (DP > 60)) // Temp Change to Allow for lower DP to build DP model from various runs over bit ranges for DP alorithm
//	{
//		printf("Unsupported DP value (%d)!\r\n", DP);
//		return false;
//	}
//
//	printf("\r\nSolving point: Range %d bits, DP %d, start...\r\n", Range, DP);
//	double ops = 1.15 * pow(2.0, Range / 2.0);
//	double dp_val = (double)(1ull << DP);
//	double ram = (32 + 4 + 4) * ops / dp_val; //+4 for grow allocation and memory fragmentation
//	ram += sizeof(TListRec) * 256 * 256 * 256; //3byte-prefix table
//	ram /= (1024 * 1024 * 1024); //GB
//	printf("SOTA method, estimated ops: 2^%.3f, RAM for DPs: %.3f GB. DP and GPU overheads not included!\r\n", log2(ops), ram);
//	gIsOpsLimit = false;
//	double MaxTotalOps = 0.0;
//	if (gMax > 0)
//	{
//		MaxTotalOps = gMax * ops;
//		double ram_max = (32 + 4 + 4) * MaxTotalOps / dp_val; //+4 for grow allocation and memory fragmentation
//		ram_max += sizeof(TListRec) * 256 * 256 * 256; //3byte-prefix table
//		ram_max /= (1024 * 1024 * 1024); //GB
//		printf("Max allowed number of ops: 2^%.3f, max RAM for DPs: %.3f GB\r\n", log2(MaxTotalOps), ram_max);
//	}
//
//	u64 total_kangs = GpuKangs[0]->CalcKangCnt();
//	for (int i = 1; i < GpuCnt; i++)
//		total_kangs += GpuKangs[i]->CalcKangCnt();
//	double path_single_kang = ops / total_kangs;
//	double DPs_per_kang = path_single_kang / dp_val;
//	printf("Estimated DPs per kangaroo: %.3f.%s\r\n", DPs_per_kang, (DPs_per_kang < 4) ? " DP overhead is big, use less DP value if possible!" : "");
//
//	if (!gGenMode && gTamesFileName[0])
//	{
//		printf("load tames...\r\n");
//		if (db.LoadFromFile(gTamesFileName))
//		{
//			printf("tames loaded\r\n");
//			if (db.Header[0] != gRange)
//			{
//				printf("loaded tames have different range, they cannot be used, clear\r\n");
//				db.Clear();
//			}
//		}
//		else
//			printf("tames loading failed\r\n");
//	}
//
//	SetRndSeed(0); //use same seed to make tames from file compatible
//	PntTotalOps = 0;
//	PntIndex = 0;
//	//prepare jumps
//	EcInt minjump, t;
//	minjump.Set(1);
//	minjump.ShiftLeft(Range / 2 + 3);
//	for (int i = 0; i < JMP_CNT; i++)
//	{
//		EcJumps1[i].dist = minjump;
//		t.RndMax(minjump);
//		EcJumps1[i].dist.Add(t);
//		EcJumps1[i].dist.data[0] &= 0xFFFFFFFFFFFFFFFE; //must be even
//		EcJumps1[i].p = ec.MultiplyG(EcJumps1[i].dist);
//	}
//
//	minjump.Set(1);
//	minjump.ShiftLeft(Range - 10); //large jumps for L1S2 loops. Must be almost RANGE_BITS
//	for (int i = 0; i < JMP_CNT; i++)
//	{
//		EcJumps2[i].dist = minjump;
//		t.RndMax(minjump);
//		EcJumps2[i].dist.Add(t);
//		EcJumps2[i].dist.data[0] &= 0xFFFFFFFFFFFFFFFE; //must be even
//		EcJumps2[i].p = ec.MultiplyG(EcJumps2[i].dist);
//	}
//
//	minjump.Set(1);
//	minjump.ShiftLeft(Range - 10 - 2); //large jumps for loops >2
//	for (int i = 0; i < JMP_CNT; i++)
//	{
//		EcJumps3[i].dist = minjump;
//		t.RndMax(minjump);
//		EcJumps3[i].dist.Add(t);
//		EcJumps3[i].dist.data[0] &= 0xFFFFFFFFFFFFFFFE; //must be even
//		EcJumps3[i].p = ec.MultiplyG(EcJumps3[i].dist);
//	}
//	SetRndSeed(GetTickCount64());
//
//	Int_HalfRange.Set(1);
//	Int_HalfRange.ShiftLeft(Range - 1);
//	Pnt_HalfRange = ec.MultiplyG(Int_HalfRange);
//	Pnt_NegHalfRange = Pnt_HalfRange;
//	Pnt_NegHalfRange.y.NegModP();
//	Int_TameOffset.Set(1);
//	Int_TameOffset.ShiftLeft(Range - 1);
//	EcInt tt;
//	tt.Set(1);
//	tt.ShiftLeft(Range - 5); //half of tame range width
//	Int_TameOffset.Sub(tt);
//	gPntToSolve = PntToSolve;
//
//	//prepare GPUs
//	for (int i = 0; i < GpuCnt; i++)
//		if (!GpuKangs[i]->Prepare(PntToSolve, Range, DP, EcJumps1, EcJumps2, EcJumps3))
//		{
//			GpuKangs[i]->Failed = true;
//			printf("GPU %d Prepare failed\r\n", GpuKangs[i]->CudaIndex);
//		}
//
//	u64 tm0 = GetTickCount64();
//	printf("GPUs started...\r\n");
//
//#ifdef _WIN32
//	HANDLE thr_handles[MAX_GPU_CNT];
//#else
//	pthread_t thr_handles[MAX_GPU_CNT];
//#endif
//
//	u32 ThreadID;
//	gSolved = false;
//	ThrCnt = GpuCnt;
//	for (int i = 0; i < GpuCnt; i++)
//	{
//#ifdef _WIN32
//		thr_handles[i] = (HANDLE)_beginthreadex(NULL, 0, kang_thr_proc, (void*)GpuKangs[i], 0, &ThreadID);
//#else
//		pthread_create(&thr_handles[i], NULL, kang_thr_proc, (void*)GpuKangs[i]);
//#endif
//	}
//
//	u64 tm_stats = GetTickCount64();
//	while (!gSolved)
//	{
//		CheckNewPoints();
//		Sleep(10);
//		if (GetTickCount64() - tm_stats > 10 * 1000)
//		{
//			ShowStats(tm0, ops, dp_val);
//			tm_stats = GetTickCount64();
//		}
//
//		if ((MaxTotalOps > 0.0) && (PntTotalOps > MaxTotalOps))
//		{
//			gIsOpsLimit = true;
//			printf("Operations limit reached\r\n");
//			break;
//		}
//	}
//
//	printf("Stopping work ...\r\n");
//	for (int i = 0; i < GpuCnt; i++)
//		GpuKangs[i]->Stop();
//	while (ThrCnt)
//		Sleep(10);
//	for (int i = 0; i < GpuCnt; i++)
//	{
//#ifdef _WIN32
//		CloseHandle(thr_handles[i]);
//#else
//		pthread_join(thr_handles[i], NULL);
//#endif
//	}
//
//	if (gIsOpsLimit)
//	{
//		if (gGenMode)
//		{
//			printf("saving tames...\r\n");
//			db.Header[0] = gRange;
//			if (db.SaveToFile(gTamesFileName))
//				printf("tames saved\r\n");
//			else
//				printf("tames saving failed\r\n");
//		}
//		db.Clear();
//		return false;
//	}
//
//	double K = (double)PntTotalOps / pow(2.0, Range / 2.0);
//	printf("Point solved, K: %.3f (with DP and GPU overheads)\r\n\r\n", K);
//	db.Clear();
//	*pk_res = gPrivKey;
//	return true;
//}



// An attempt to reduce the # of calcs performed by SolvePoint
// Check on whether the SolvePoint function could be optimized by 
// reducing redundant calculations and improving the efficiency of the loop.
bool SolvePoint(EcPoint PntToSolve, int Range, int DP, EcInt* pk_res)
{
	if ((Range < 19) || (Range > 160))
	{
		printf("Unsupported Range value (%d)!\r\n", Range);
		return false;
	}
	if ((DP < 4) || (DP > 60)) // Temp Change to Allow for lower DP to build DP model from various runs over bit ranges for DP alorithm
	{
		printf("Unsupported DP value (%d)!\r\n", DP);
		return false;
	}

	printf("\r\nSolving point: Range %d bits, DP %d, start...\r\n", Range, DP);
	double ops = 1.15 * pow(2.0, Range / 2.0);
	double dp_val = (double)(1ull << DP);
	double ram = (32 + 4 + 4) * ops / dp_val; //+4 for grow allocation and memory fragmentation
	ram += sizeof(TListRec) * 256 * 256 * 256; //3byte-prefix table
	ram /= (1024 * 1024 * 1024); //GB
	printf("SOTA method, estimated ops: 2^%.3f, RAM for DPs: %.3f GB. DP and GPU overheads not included!\r\n", log2(ops), ram);
	gIsOpsLimit = false;
	double MaxTotalOps = 0.0;
	if (gMax > 0)
	{
		MaxTotalOps = gMax * ops;
		double ram_max = (32 + 4 + 4) * MaxTotalOps / dp_val; //+4 for grow allocation and memory fragmentation
		ram_max += sizeof(TListRec) * 256 * 256 * 256; //3byte-prefix table
		ram_max /= (1024 * 1024 * 1024); //GB
		printf("Max allowed number of ops: 2^%.3f, max RAM for DPs: %.3f GB\r\n", log2(MaxTotalOps), ram_max);
	}

	u64 total_kangs = GpuKangs[0]->CalcKangCnt();
	for (int i = 1; i < GpuCnt; i++)
		total_kangs += GpuKangs[i]->CalcKangCnt();
	double path_single_kang = ops / total_kangs;
	double DPs_per_kang = path_single_kang / dp_val;
	printf("Estimated DPs per kangaroo: %.3f.%s\r\n", DPs_per_kang, (DPs_per_kang < 5) ? " DP overhead is big, use less DP value if possible!" : "");

	if (!gGenMode && gTamesFileName[0])
	{
		printf("load tames...\r\n");
		if (db.LoadFromFile(gTamesFileName))
		{
			printf("tames loaded\r\n");
			if (db.Header[0] != gRange)
			{
				printf("loaded tames have different range, they cannot be used, clear\r\n");
				db.Clear();
			}
		}
		else
			printf("tames loading failed\r\n");
	}

	SetRndSeed(0); //use same seed to make tames from file compatible
	PntTotalOps = 0;
	PntIndex = 0;
	//prepare jumps
	EcInt minjump, t;
	minjump.Set(1);
	minjump.ShiftLeft(Range / 2 + 3);
	for (int i = 0; i < JMP_CNT; i++)
	{
		EcJumps1[i].dist = minjump;
		t.RndMax(minjump);
		EcJumps1[i].dist.Add(t);
		EcJumps1[i].dist.data[0] &= 0xFFFFFFFFFFFFFFFE; //must be even
		EcJumps1[i].p = ec.MultiplyG(EcJumps1[i].dist);
	}

	minjump.Set(1);
	minjump.ShiftLeft(Range - 10); //large jumps for L1S2 loops. Must be almost RANGE_BITS
	for (int i = 0; i < JMP_CNT; i++)
	{
		EcJumps2[i].dist = minjump;
		t.RndMax(minjump);
		EcJumps2[i].dist.Add(t);
		EcJumps2[i].dist.data[0] &= 0xFFFFFFFFFFFFFFFE; //must be even
		EcJumps2[i].p = ec.MultiplyG(EcJumps2[i].dist);
	}

	minjump.Set(1);
	minjump.ShiftLeft(Range - 10 - 2); //large jumps for loops >2
	for (int i = 0; i < JMP_CNT; i++)
	{
		EcJumps3[i].dist = minjump;
		t.RndMax(minjump);
		EcJumps3[i].dist.Add(t);
		EcJumps3[i].dist.data[0] &= 0xFFFFFFFFFFFFFFFE; //must be even
		EcJumps3[i].p = ec.MultiplyG(EcJumps3[i].dist);
	}
	SetRndSeed(GetTickCount64());

	Int_HalfRange.Set(1);
	Int_HalfRange.ShiftLeft(Range - 1);
	Pnt_HalfRange = ec.MultiplyG(Int_HalfRange);
	Pnt_NegHalfRange = Pnt_HalfRange;
	Pnt_NegHalfRange.y.NegModP();
	Int_TameOffset.Set(1);
	Int_TameOffset.ShiftLeft(Range - 1);
	EcInt tt;
	tt.Set(1);
	tt.ShiftLeft(Range - 5); //half of tame range width
	Int_TameOffset.Sub(tt);
	gPntToSolve = PntToSolve;

	//prepare GPUs
	for (int i = 0; i < GpuCnt; i++)
		if (!GpuKangs[i]->Prepare(PntToSolve, Range, DP, EcJumps1, EcJumps2, EcJumps3))
		{
			GpuKangs[i]->Failed = true;
			printf("GPU %d Prepare failed\r\n", GpuKangs[i]->CudaIndex);
		}

	u64 tm0 = GetTickCount64();
	printf("GPUs started...\r\n");

#ifdef _WIN32
	HANDLE thr_handles[MAX_GPU_CNT];
#else
	pthread_t thr_handles[MAX_GPU_CNT];
#endif

	u32 ThreadID;
	gSolved = false;
	ThrCnt = GpuCnt;
	for (int i = 0; i < GpuCnt; i++)
	{
#ifdef _WIN32
		thr_handles[i] = (HANDLE)_beginthreadex(NULL, 0, kang_thr_proc, (void*)GpuKangs[i], 0, &ThreadID);
#else
		pthread_create(&thr_handles[i], NULL, kang_thr_proc, (void*)GpuKangs[i]);
#endif
	}

	u64 tm_stats = GetTickCount64();
	while (!gSolved)
	{
		CheckNewPoints();
		Sleep(10);
		if (GetTickCount64() - tm_stats > 10 * 1000)
		{
			ShowStats(tm0, ops, dp_val);
			tm_stats = GetTickCount64();
		}

		if ((MaxTotalOps > 0.0) && (PntTotalOps > MaxTotalOps))
		{
			gIsOpsLimit = true;
			printf("Operations limit reached\r\n");
			break;
		}
	}

	printf("Stopping work ...\r\n");
	for (int i = 0; i < GpuCnt; i++)
		GpuKangs[i]->Stop();
	while (ThrCnt)
		Sleep(10);
	for (int i = 0; i < GpuCnt; i++)
	{
#ifdef _WIN32
		CloseHandle(thr_handles[i]);
#else
		pthread_join(thr_handles[i], NULL);
#endif
	}

	if (gIsOpsLimit)
	{
		if (gGenMode)
		{
			printf("saving tames...\r\n");
			db.Header[0] = gRange;
			if (db.SaveToFile(gTamesFileName))
				printf("tames saved\r\n");
			else
				printf("tames saving failed\r\n");
		}
		db.Clear();
		return false;
	}

	double K = (double)PntTotalOps / pow(2.0, Range / 2.0);
	printf("Point solved, K: %.3f (with DP and GPU overheads)\r\n\r\n", K);
	db.Clear();
	*pk_res = gPrivKey;
	return true;
}

/**
 * @brief Parses the command line arguments.
 *
 * @param argc The number of arguments.
 * @param argv The argument values.
 * @return true If the arguments are valid.
 * @return false Otherwise.
 */

bool ParseCommandLine(int argc, char* argv[])
{
	int ci = 1;
	while (ci < argc)
	{
		char* argument = argv[ci];
		ci++;
		if (strcmp(argument, "-gpu") == 0)
		{
			if (ci >= argc)
			{
				printf("error: missed value after -gpu option\r\n");
				return false;
			}
			char* gpus = argv[ci];
			ci++;
			memset(gGPUs_Mask, 0, sizeof(gGPUs_Mask));
			for (int i = 0; i < (int)strlen(gpus); i++)
			{
				if ((gpus[i] < '0') || (gpus[i] > '9'))
				{
					printf("error: invalid value for -gpu option\r\n");
					return false;
				}
				gGPUs_Mask[gpus[i] - '0'] = 1;
			}
		}
		else
			if (strcmp(argument, "-dp") == 0)
			{
				int val = atoi(argv[ci]);
				ci++;
				if ((val < 4) || (val > 60)) // Temp Change to Allow for lower DP to build DP model from various runs over bit ranges for DP alorithm
				{
					printf("error: invalid value for -dp option\r\n");
					return false;
				}
				gDP = val;
			}
			else
				if (strcmp(argument, "-range") == 0)
				{
					int val = atoi(argv[ci]);
					ci++;
					if ((val < 19) || (val > 160))
					{
						printf("error: invalid value for -range option\r\n");
						return false;
					}
					gRange = val;
				}
				else
					if (strcmp(argument, "-start") == 0)
					{
						if (!gStart.SetHexStr(argv[ci]))
						{
							printf("error: invalid value for -start option\r\n");
							return false;
						}
						ci++;
						gStartSet = true;
					}
					else
						if (strcmp(argument, "-pubkey") == 0)
						{
							if (!gPubKey.SetHexStr(argv[ci]))
							{
								printf("error: invalid value for -pubkey option\r\n");
								return false;
							}
							ci++;
						}
						else
							if (strcmp(argument, "-tames") == 0)
							{
								strcpy(gTamesFileName, argv[ci]);
								ci++;
							}
							else
								if (strcmp(argument, "-max") == 0)
								{
									double val = atof(argv[ci]);
									ci++;
									if (val < 0.00001)
									{
										printf("error: invalid value for -max option\r\n");
										return false;
									}
									gMax = val;
								}
								else
								{
									printf("error: unknown option %s\r\n", argument);
									return false;
								}
	}
	if (!gPubKey.x.IsZero())
		if (!gStartSet || !gRange || !gDP)
		{
			printf("error: you must also specify -dp, -range and -start options\r\n");
			return false;
		}
	if (gTamesFileName[0] && !IsFileExist(gTamesFileName))
	{
		if (gMax == 0.0)
		{
			printf("error: you must also specify -max option to generate tames\r\n");
			return false;
		}
		gGenMode = true;
	}
	return true;
}

/**
 * @brief The main function of the program.
 *
 * @param argc The number of arguments.
 * @param argv The argument values.
 * @return int The exit code.
 */
int main(int argc, char* argv[])
{
#ifdef _DEBUG	
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	printf("********************************************************************************\r\n");
	printf("*                    RCKangaroo v3.0  (c) 2024 RetiredCoder                    *\r\n");
	printf("*                    RCKangaroo v3.2  2025.02.10 - jlggps                      *\r\n");
	printf("********************************************************************************\r\n\r\n");

	printf("This software is free and open-source: https://github.com/RetiredC\r\n");
	printf("Minor modifications to code by jlggps: https://github.com/jlggnss\r\n");
	printf("It demonstrates fast GPU implementation of SOTA Kangaroo method for solving ECDLP\r\n");

#ifdef _WIN32
	printf("Windows version\r\n");
#else
	printf("Linux version\r\n");
#endif

#ifdef DEBUG_MODE
	printf("DEBUG MODE\r\n\r\n");
#endif

	InitEc();
	gDP = 0;
	gRange = 0;
	gStartSet = false;
	gTamesFileName[0] = 0;
	gMax = 0.0;
	gGenMode = false;
	gIsOpsLimit = false;
	memset(gGPUs_Mask, 1, sizeof(gGPUs_Mask));
	if (!ParseCommandLine(argc, argv))
		return 0;

	InitGpus();

	if (!GpuCnt)
	{
		printf("No supported GPUs detected, exit\r\n");
		return 0;
	}

	pPntList = (u8*)malloc(MAX_CNT_LIST * GPU_DP_SIZE);
	pPntList2 = (u8*)malloc(MAX_CNT_LIST * GPU_DP_SIZE);
	TotalOps = 0;
	TotalSolved = 0;
	gTotalErrors = 0;
	IsBench = gPubKey.x.IsZero();

	if (!IsBench && !gGenMode)
	{
		printf("\r\nMAIN MODE\r\n\r\n");
		EcPoint PntToSolve, PntOfs;
		EcInt pk, pk_found;

		PntToSolve = gPubKey;
		if (!gStart.IsZero())
		{
			PntOfs = ec.MultiplyG(gStart);
			PntOfs.y.NegModP();
			PntToSolve = ec.AddPoints(PntToSolve, PntOfs);
		}

		char sx[100], sy[100];
		gPubKey.x.GetHexStr(sx);
		gPubKey.y.GetHexStr(sy);
		printf("Solving public key\r\nX: %s\r\nY: %s\r\n", sx, sy);
		gStart.GetHexStr(sx);
		printf("Offset: %s\r\n", sx);

		if (!SolvePoint(PntToSolve, gRange, gDP, &pk_found))
		{
			if (!gIsOpsLimit)
				printf("FATAL ERROR: SolvePoint failed\r\n");
			goto label_end;
		}
		pk_found.AddModP(gStart);
		EcPoint tmp = ec.MultiplyG(pk_found);
		if (!tmp.IsEqual(gPubKey))
		{
			printf("FATAL ERROR: SolvePoint found incorrect key\r\n");
			goto label_end;
		}
		//happy end
		char s[100];
		pk_found.GetHexStr(s);
		printf("\r\nPRIVATE KEY: %s\r\n\r\n", s);
		FILE* fp = fopen("RESULTS.TXT", "a");
		if (fp)
		{
			fprintf(fp, "PRIVATE KEY: %s\n", s);
			fclose(fp);
		}
		else //we cannot save the key, show error and wait forever so the key is displayed
		{
			printf("WARNING: Cannot save the key to RESULTS.TXT!\r\n");
			while (1)
				Sleep(100);
		}
	}
	else
	{
		if (gGenMode)
			printf("\r\nTAMES GENERATION MODE\r\n");
		else
			printf("\r\nBENCHMARK MODE\r\n");
		//solve points, show K
		while (1)
		{
			EcInt pk, pk_found;
			EcPoint PntToSolve;

			if (!gRange)
				gRange = 78;
			if (!gDP)
				gDP = 16;

			//generate random pk
			pk.RndBits(gRange);
			PntToSolve = ec.MultiplyG(pk);

			if (!SolvePoint(PntToSolve, gRange, gDP, &pk_found))
			{
				if (!gIsOpsLimit)
					printf("FATAL ERROR: SolvePoint failed\r\n");
				break;
			}
			if (!pk_found.IsEqual(pk))
			{
				printf("FATAL ERROR: Found key is wrong!\r\n");
				break;
			}
			TotalOps += PntTotalOps;
			TotalSolved++;
			u64 ops_per_pnt = TotalOps / TotalSolved;
			double K = (double)ops_per_pnt / pow(2.0, gRange / 2.0);
			printf("Points solved: %d, average K: %.3f (with DP and GPU overheads)\r\n", TotalSolved, K);
			//if (TotalSolved >= 100) break; //dbg
		}
	}
label_end:
	for (int i = 0; i < GpuCnt; i++)
		delete GpuKangs[i];
	DeInitEc();
	free(pPntList2);
	free(pPntList);
}

