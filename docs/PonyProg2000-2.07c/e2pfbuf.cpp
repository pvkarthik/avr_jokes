//=========================================================================//
//                                                                         //
//  PonyProg - Serial Device Programmer                                    //
//                                                                         //
//  Copyright (C) 1997-2007   Claudio Lanconelli                           //
//                                                                         //
//  http://ponyprog.sourceforge.net                                        //
//                                                                         //
//-------------------------------------------------------------------------//
// $Id: e2pfbuf.cpp,v 1.3 2007/04/20 10:58:21 lancos Exp $
//-------------------------------------------------------------------------//
//                                                                         //
// This program is free software; you can redistribute it and/or           //
// modify it under the terms of the GNU  General Public License            //
// as published by the Free Software Foundation; either version2 of        //
// the License, or (at your option) any later version.                     //
//                                                                         //
// This program is distributed in the hope that it will be useful,         //
// but WITHOUT ANY WARRANTY; without even the implied warranty of          //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       //
// General Public License for more details.                                //
//                                                                         //
// You should have received a copy of the GNU  General Public License      //
// along with this program (see COPYING);     if not, write to the         //
// Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. //
//                                                                         //
//-------------------------------------------------------------------------//
//=========================================================================//

#include <stdio.h>
#include <string.h>

#include "e2pfbuf.h"		// Header file
#include "crc.h"
#include "errcode.h"

#include "e2phead.h"
#include "eeptypes.h"

static char const *id_string = "E2P!Lanc";

//======================>>> e2pFileBuf::e2pFileBuf <<<=======================
e2pFileBuf::e2pFileBuf(e2AppWinInfo *wininfo)
		: FileBuf(wininfo)
{
	file_type = E2P;
}

//======================>>> e2pFileBuf::~e2pFileBuf <<<=======================
e2pFileBuf::~e2pFileBuf()
{
}

//======================>>> e2pFileBuf::Load <<<=======================
int e2pFileBuf::Load(int loadtype, long relocation_offset)
{
	int GetE2PSubType(long x);
	int GetE2PPriType(long x);

	FILE *fh;
	e2pHeader hdr;
	int rval;

	if ( (fh = fopen(FileBuf::GetFileName(), "rb")) == NULL )
		return FILENOTFOUND;

	// Controlla il tipo di file
	if ( fread(&hdr, sizeof(e2pHeader), 1, fh) &&
				strncmp(hdr.fileID, id_string, E2P_ID_SIZE) == 0 )
	{
		unsigned char *localbuf;
		localbuf = new unsigned char[hdr.e2pSize];
		if (localbuf)
		{
			//Controlla il CRC dell'Header
			if ( mcalc_crc(&hdr, sizeof(hdr)-sizeof(hdr.headCrc)) == hdr.headCrc &&
				//Controlla il CRC della memoria
				fcalc_crc(fh, sizeof(e2pHeader), 0) == hdr.e2pCrc &&
				//Legge il contenuto nel buffer
				fread(localbuf, hdr.e2pSize, 1, fh) )
	//			fread(FileBuf::GetBufPtr(), hdr.e2pSize, 1, fh) )
			{
				SetEEpromType(GetE2PPriType(hdr.e2pType), GetE2PSubType(hdr.e2pType));	//Questa imposta il tipo di eeprom, e indirettamente la dimensione del block
	//			FileBuf::SetNoOfBlock( hdr.e2pSize / FileBuf::GetBlockSize() );

				if (hdr.fversion > 0)
				{
					SetLockBits( ((DWORD)hdr.e2pExtLockBits << 8) | hdr.e2pLockBits );
					SetFuseBits( ((DWORD)hdr.e2pExtFuseBits << 8) | hdr.e2pFuseBits );
				}
				else
				{	//Old file version
					if (GetE2PPriType(hdr.e2pType) == PIC16XX ||
						GetE2PPriType(hdr.e2pType) == PIC168XX ||
						GetE2PPriType(hdr.e2pType) == PIC125XX )
					{
						SetLockBits( ((DWORD)hdr.e2pLockBits << 8) | hdr.e2pFuseBits );
					}
					else
					{
						SetLockBits(hdr.e2pLockBits);
						SetFuseBits(hdr.e2pFuseBits);
					}

				}

				if (hdr.fversion > 1)
				{
					SetSplitted( ((DWORD)hdr.split_size_High << 16) | hdr.split_size_Low );
				}
				else
				{
					SetSplitted(hdr.split_size_Low);
				}

				SetStringID(hdr.e2pStringID);
				SetComment(hdr.e2pComment);
				SetRollOver(hdr.flags & 7);
				SetCRC(hdr.e2pCrc);

				//Copy the content into the buffer
				if (loadtype == ALL_TYPE)
				{
					if ( hdr.e2pSize <= GetBufSize() )
						memcpy(FileBuf::GetBufPtr(), localbuf, hdr.e2pSize);
				}
				else
				if (loadtype == PROG_TYPE)
				{
					long s = GetSplitted();
					if ( s <= 0 )
						s = hdr.e2pSize;

					//if splittedInfo == 0 then copy ALL
					if ( s <= hdr.e2pSize && s <= GetBufSize() )
						memcpy(FileBuf::GetBufPtr(), localbuf, s);
				}
				else
				if (loadtype == DATA_TYPE)
				{
					long s = GetSplitted();
					if ( s >= 0 &&
							s < hdr.e2pSize &&
							hdr.e2pSize <= GetBufSize() )
						memcpy(FileBuf::GetBufPtr() + s, localbuf + s, hdr.e2pSize - s);
				}

				rval = GetNoOfBlock();
			}
			else
				rval = READERROR;

			delete localbuf;
		}
		else
			rval = OUTOFMEMORY;
	}
	else
		rval = BADFILETYPE;

	fclose(fh);
	return rval;
}

//======================>>> e2pFileBuf::Save <<<=======================
int e2pFileBuf::Save(int savetype, long relocation_offset)
{
	FILE *fh;
	e2pHeader hdr;
	int rval, create_file = 0;

	if (FileBuf::GetNoOfBlock() <= 0)
		return NOTHINGTOSAVE;

	fh = fopen(FileBuf::GetFileName(), "r+b");
	if (fh == NULL)
	{
		fh = fopen(FileBuf::GetFileName(), "w+b");
		if (fh == NULL)
			return CREATEERROR;

		create_file = 1;
	}

	//Header settings
	memset(&hdr, 0, sizeof(hdr));		//Clear all to zero first
	strncpy(hdr.fileID, id_string, E2P_ID_SIZE);	//Id
	hdr.e2pSize = FileBuf::GetNoOfBlock() * FileBuf::GetBlockSize();

	unsigned char *localbuf;
	localbuf = new unsigned char[hdr.e2pSize];
	if (localbuf)
	{
		long s = GetSplitted();

		int rv = 0;

		if (!create_file)
		{
			//Initialize local buffer
			//  if the file already exist read the current content
			//  otherwise set the localbuffer to 0xFF
			rv = fseek(fh, sizeof(hdr), SEEK_SET);
			if (rv == 0)
			{
				rv = fread(localbuf, hdr.e2pSize, 1, fh);
			}
			else
				rv = 0;
			rewind(fh);
		}

		if (!rv)
			memset(localbuf, 0xff, hdr.e2pSize);

		if (savetype == ALL_TYPE)
		{
			memcpy(localbuf, FileBuf::GetBufPtr(), hdr.e2pSize);
		}
		else
		if (savetype == DATA_TYPE)
		{
			if (hdr.e2pSize > s)
				memcpy(localbuf + s, FileBuf::GetBufPtr() + s, hdr.e2pSize - s);
		}
		else
		if (savetype == PROG_TYPE)
		{
			if (s > 0 &&  s <= hdr.e2pSize)
				memcpy(localbuf, FileBuf::GetBufPtr(), s);
		}

		hdr.fversion = E2P_FVERSION;

		hdr.e2pLockBits = (BYTE)(GetLockBits() & 0xFF);
		hdr.e2pExtLockBits = (WORD)(GetLockBits() >> 8);
		hdr.e2pFuseBits = (BYTE)(GetFuseBits() & 0xFF);
		hdr.e2pExtFuseBits = (WORD)(GetFuseBits() >> 8);

		hdr.e2pType = GetEEpromType();
		GetStringID(hdr.e2pStringID);
		GetComment(hdr.e2pComment);
		hdr.flags = GetRollOver() & 7;
		hdr.split_size_Low = (UWORD)GetSplitted();
		hdr.split_size_High = (UWORD)(GetSplitted() >> 16);
		hdr.e2pCrc = mcalc_crc(localbuf, hdr.e2pSize);
		hdr.headCrc = mcalc_crc(&hdr, sizeof(hdr)-sizeof(hdr.headCrc));

		//Write to file
		if (	fwrite(&hdr, sizeof(hdr), 1, fh) &&		//Write the header
				fwrite(localbuf, hdr.e2pSize, 1, fh) )	//Write the buffer
		{
			rval = GetNoOfBlock();
		}
		else
			rval = WRITEERROR;

		delete localbuf;
	}
	else
		rval = OUTOFMEMORY;

	fclose(fh);
	return rval;
}
