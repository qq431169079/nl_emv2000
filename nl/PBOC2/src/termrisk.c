/**
* @file TermRisk.c
* @brief 终端风险管理

* @version Version 1.0
* @author 叶明统
* @date 2005-08-05
*/

#include <string.h>
#include <stdlib.h>
#include "posapi.h"
#include "posdef.h"
#include "define.h"
#include "tvrtsi.h"
#include "ic.h"
#include "AppData.h"
#include "tools.h"
#include "TermRisk.h"

extern TTermParam g_termparam ;

void _PreTermRiskManage(TTermRisk * pTermRisk)
{
	pTermRisk->pPAN = GetAppData("\x5a", NULL);
	pTermRisk->pAIP = GetAppData("\x82", NULL);
	pTermRisk->Lower_COL= GetAppData("\x9f\x14", NULL);
	pTermRisk->Upper_COL= GetAppData("\x9f\x23", NULL);
	pTermRisk->MaxTargetPercent = g_termparam.MaxTargetPercent ;
	pTermRisk->TargetPercent = g_termparam.TargetPercent ;
	pTermRisk->FloorLimt = g_termparam.FloorLimit ;
	pTermRisk->ThresholdValue = g_termparam.ThresholdValue ;

}

//查询卡号是否在黑名单中
// SUCC 不在黑名单中  FAIL 在黑名单中
int	BlackListExcepFile(char *pPAN)
{
	int fp;
	int len;
	TBlackCard blackcard;
	
	//打开黑名单文件
	fp = fopen(BlackListFile, "r");
	if(fp < 0)
	{
		return SUCC;
	}

	while(1)
	{
		len = fread((char *)&blackcard, sizeof(TBlackCard), fp);
		if(len != sizeof(TBlackCard))
		{
			break;			
		}

		if(memcmp(pPAN, blackcard.cardno, blackcard.len)==0)
		{
			fclose(fp);
			SetTVR(CARD_IN_EXCEP_FILE, 1);
			return FAIL;
		}
	}
	
	fclose(fp);
	return SUCC;
}

/**
* @fn Floor_Limit
* @brief 最低限额
  
  计算交易金额（包括当前的与最近使用的），判断是否超过
  交易限额，并置相应的TVR位
* @param (in) const char * FL_TERM 终端交易限额
* @param (in) int iAmount 当前交易金额（授权金额）
* @return SUCC <最低限额
          FAIL >=最低限额
*/
int FloorLimit(const char * FL_TERM, int iAmount)
{
	int amount = 0;
	int termlimit ;
	
	C4ToInt( (unsigned int *)&termlimit, (unsigned char *)FL_TERM ) ;
//	amount = getlatestamount() ;
	if ( amount + iAmount >= termlimit )
	{
		SetTVR(TRADE_EXCEED_FLOOR_LIMIT, 1) ;
		return FAIL ;
	}
	return SUCC ;
}

/**
* @fn RandTransSelect
* @brief 随机交易选择
  
* @param (in) const TTermRisk *  pTermRisk 指向终端风险管理用到的数据敬尾僮饕褂玫挠�
* @param (in) int iAmount 当前交易金额
* @return void
*/
void RandTransSelect( const TTermRisk * pTermRisk, int iAmount )
{
	int termfl ;				// 终端最低限额 
	int ThresValue ;			// 偏置随即阈值
	float TransTargPercent ;	// 交易目标百分比 
	float InterpolationFactor ; // 插值因子 
	int RandomPercent ;			// 随机百分数 
	struct postime pt ;

	C4ToInt((unsigned int *)&termfl, (unsigned char *)pTermRisk->FloorLimt) ;
	C4ToInt((unsigned int *)&ThresValue, (unsigned char *)pTermRisk->ThresholdValue) ;

	getpostime(&pt) ;
	srand(pt.second + pt.minute * 60 + pt.hour * 60 * 60) ;
	RandomPercent = rand() % 99 + 1 ;

	if (iAmount < ThresValue) //交易金额小于随机偏置阈值
	{
		if (RandomPercent <= pTermRisk->TargetPercent) //随机百分数小于目标百分数
		{
			SetTVR(SELECT_ONLILNE_RANDOM, 1) ;
		}
	}
	else if (iAmount < termfl) // 交易金额大于随机偏置阈值，小于最低限额
	{
		InterpolationFactor = (float)(iAmount - ThresValue) /    //插值因子
			(float)(termfl - ThresValue) ;
		TransTargPercent = (float)(pTermRisk->MaxTargetPercent -  //交易目标白分数
			pTermRisk->TargetPercent) * InterpolationFactor + 
			pTermRisk->TargetPercent ;
		if ( (float)RandomPercent <= TransTargPercent )
		{
			SetTVR(SELECT_ONLILNE_RANDOM, 1) ;
		}
	}
}

/**
* @fn GetATC
* @brief 获取应用交易序号，包括当前的，和最近联机的
 
* @param (out) char * atc 返回当前ATC 
* @param (out) char * onlineatc 返回最近联机ATC
* @return 0 二者都没有取到
          1 取到atc
		  2 取到onlineatc
		  3 同时取到二者
*/
int GetATC( char * atc, char * onlineatc )
{
	int ret ;
	char oData[256] ;
	int iExist = 0 ;
    
	/* 取ATC */
	memset(oData, 0, 10) ;
	ret = IC_GetData( GETDATAMODE_ATC, oData ) ;
	if ( ret > 0)
	{
		iExist |= 0x01 ;
		memcpy( atc, oData + 3, 2 ) ;
	}
    /* 取Latest Online ATC */
	memset(oData, 0, 10) ;
	ret = IC_GetData( GETDATAMODE_ONLINEATC, oData ) ;
	if ( ret > 0)
	{
		iExist |= 0x02 ;
		memcpy( onlineatc, oData + 3, 2 ) ;
	}
	return iExist ;
}
/**
* @fn CheckVelocity
* @brief 频度检查
 
* @param (in) const TTermRisk *  pTermRisk 指向终端风险管理用到的数据
* @return SUCC 已检查
          FAIL 没有检查
*/
int CheckVelocity( const TTermRisk *  pTermRisk ) 
{
	int ret ;
	char atc[2] ; // 应用交易序号 
	char lastonlineatc[2] ; // 最近联机应用交易序号 
	int atctmp, loatctmp ;

	// 不存在连续脱机交易下限或上限
	if ((pTermRisk->Lower_COL == NULL)||
		(pTermRisk->Upper_COL == NULL))
	{
		return FAIL ; //跳过此节
	}
	
	ret = GetATC(atc, lastonlineatc) ;
	if ( ret & 0x02 )
	{
		loatctmp = (((int)lastonlineatc[0]) << 8) + lastonlineatc[1] ;
		if ( loatctmp == 0 )		//上次联机交易序号寄存器为0
		{
			SetTVR(NEW_CARD, 1) ;	// 设置新卡标志
		}
	}
	if ( ret != 3) // 无法从ICC同时取到atc 和 lastonlineatc 
	{
		SetTVR(EXCEED_CON_OFFLINE_FLOOR_LIMIT, 1) ; //超过连续脱机下限
		SetTVR(EXCEED_CON_OFFLINE_UPPER_LIMIT, 1) ; //超过连续脱机上限
		SetTVR(ICC_DATA_LOST, 1) ; //  IC卡数据缺失 
	}
	else
	{
		atctmp = (((int)atc[0]) << 8) + atc[1] ;
		if (atctmp <= loatctmp)
		{
			SetTVR(EXCEED_CON_OFFLINE_FLOOR_LIMIT, 1) ;
			SetTVR(EXCEED_CON_OFFLINE_UPPER_LIMIT, 1) ;
		}
		if ( atctmp - loatctmp > *(pTermRisk->Lower_COL) ) //如果大于连续脱机下限
		{
			SetTVR(EXCEED_CON_OFFLINE_FLOOR_LIMIT, 1) ;
		}
		if ( atctmp - loatctmp > *(pTermRisk->Upper_COL) ) //如果大于连续脱机上限
		{
			SetTVR(EXCEED_CON_OFFLINE_UPPER_LIMIT, 1) ;
		}
	}
	return SUCC ;
}

/**
* @fn TermRisk_Manage
* @brief 频度检查
 
* @param (in) TTermRisk * pTermRisk  终端风险管理用到的数据
* @param (in) int iAmount 当前交易金额(授权金额)
* @return SUCC 已检查
          FAIL 没有检查
*/
int TermRiskManage(const TTermRisk * pTermRisk , int iAmount )
{
	int ret ;
	if ( *(pTermRisk->pAIP) & 0x08 )
	{
		BlackListExcepFile(pTermRisk->pPAN) ; //终端黑名单和异常文件处理
		if (g_termparam.cAllowForceOnline)
		{
			clrscr() ;
			printf("是否强迫联机?\n") ;
			printf("是(确认). \n否(取消).") ;
			while(1) 
			{
				ret = getkeycode(0) ;
				if (ret == ENTER || ret == ESC)
					break ;
			}
			if (ret == ENTER)
				SetTVR(MERCHANT_REQ_ONLINE, 1) ;
		}
		FloorLimit(pTermRisk->FloorLimt, iAmount) ; //最低限额检查
		RandTransSelect(pTermRisk, iAmount) ; //随即交易选择
		CheckVelocity(pTermRisk) ; //频度检查
		SetTSI(TERM_RISK_MANA_COMPLETION, 1) ;
	}
	return SUCC ;
}
