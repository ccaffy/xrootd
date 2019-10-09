#include "XrdSfs/XrdSfsGPFile.hh"
//-----------------------------------------------------------------------------
//! Read page bytes into a buffer and return corresponding checksums.
//!
//! @param  offset  - The offset where the read is to start. It must be
//!                   page aligned.
//! @param  buffer  - pointer to buffer where the bytes are to be placed.
//! @param  rdlen   - The number of bytes to read. The amount must be an
//!                   integral number of XrdSfsPageSize bytes.
//! @param  csvec   - A vector of [rdlen/XrdSfsPageSize] entries which will be
//!                   filled with the corresponding CRC32 checksum for each
//!                   page. A nil pointer does not return the checksums.
//! @param  verify  - When true, the checksum is verified for each page; an
//!                   error is returned if any checksum is incorrect.
//!
//!
//! @return >= 0      The number of bytes that placed in buffer.
//! @return SFS_ERROR File could not be read, error holds the reason.
//-----------------------------------------------------------------------------

virtual XrdSfsXferSize pgRead(XrdSfsFileOffset   offset,
                              char              *buffer,
                              XrdSfsXferSize     rdlen,
                              uint32_t          *csvec,
                              bool               verify=true)
                             {(void)offset; (void)buffer; (void)rdlen;
                              (void)csvec;  (void)verify;
                              error.setErrInfo(ENOTSUP, "Not supported.");
                              return SFS_ERROR;
                             }

//-----------------------------------------------------------------------------
//! Write page bytes into a file with corresponding checksums.
//!
//! @param  offset  - The offset where the write is to start. It must be
//!                   page aligned.
//! @param  buffer  - pointer to buffer containing the bytes to write.
//! @param  wrlen   - The number of bytes to write. If amount is not an
//!                   integral number of XrdSfsPageSize bytes, then this must
//!                   be the last write to the file at or above the offset.
//! @param  csvec   - A vector of [CEILING(wrlen/XrdSfsPageSize)] entries which
//!                   contain the corresponding CRC32 checksum for each page.
//!                   A nil pointer causes the checksums to be computed.
//! @param  verify  - When true, the checksum in csvec is verified for each
//!                   page; and error is returned if any checksum is incorrect.
//!
//!
//! @return >= 0      The number of bytes written.
//! @return SFS_ERROR File could not be read, error holds the reason.
//-----------------------------------------------------------------------------

virtual XrdSfsXferSize pgWrite(XrdSfsFileOffset   offset,
                               char              *buffer,
                               XrdSfsXferSize     wrlen,
                               uint32_t          *csvec,
                               bool               verify=true)
                              {(void)offset; (void)buffer; (void)wrlen;
                               (void)csvec;  (void)verify;
                               error.setErrInfo(ENOTSUP, "Not supported.");
                               return SFS_ERROR;
                              }

//-----------------------------------------------------------------------------
//! Notify filesystem that a client has connected.
//!
//! @param  client - Client's identify (see common description).
//-----------------------------------------------------------------------------

virtual void           Connect(const XrdSecEntity     *client = 0)
{
  (void)client;
}

//! @return The bit-wise feature set (i.e. supported or configured).
//!         See include file XrdSfsFlags.hh for actual bit values.
//! Perform a third party file transfer or cancel one.
//! @param  gpAct  - What to do as one of the enums listed below.
//! @param  gpReq  - reference tothe object describing the request. This object
//!                  is also used communicate the request status.
//! @param  eInfo  - The object where error info or results are to be returned.
//! @param  client - Client's identify (see common description).
//! @return SFS_OK   Request accepted (same as SFS_STARTED). Otherwise, one of
//!                  SFS_ERROR, SFS_REDIRECT, or SFS_STALL.
enum gpfFunc {gpfCancel=0, //!< Cancel this request
              gpfGet,      //!< Perform a file retrieval
              gpfPut       //!< Perform a file push
             };

virtual int            gpFile(      gpfFunc          &gpAct,
                                    XrdSfsGPFile     &gpReq,
                                    XrdOucErrInfo    &eInfo,
                              const XrdSecEntity     *client = 0)
                             {(void)gpAct, (void)gpReq; (void)client;
                              eInfo.setErrInfo(ENOTSUP, "Not supported.");
                              return SFS_ERROR;
                             }
//! Prepare a file for future processing.