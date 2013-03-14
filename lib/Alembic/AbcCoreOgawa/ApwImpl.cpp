//-*****************************************************************************
//
// Copyright (c) 2013,
//  Sony Pictures Imageworks, Inc. and
//  Industrial Light & Magic, a division of Lucasfilm Entertainment Company Ltd.
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *       Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *       Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// *       Neither the name of Sony Pictures Imageworks, nor
// Industrial Light & Magic nor the names of their contributors may be used
// to endorse or promote products derived from this software without specific
// prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//-*****************************************************************************

#include <Alembic/AbcCoreOgawa/ApwImpl.h>
#include <Alembic/AbcCoreOgawa/WriteUtil.h>

namespace Alembic {
namespace AbcCoreOgawa {
namespace ALEMBIC_VERSION_NS {

//-*****************************************************************************
ApwImpl::ApwImpl( AbcA::CompoundPropertyWriterPtr iParent,
                  Ogawa::OGroupPtr iGroup,
                  PropertyHeaderPtr iHeader ) :
    m_parent( iParent ), m_header( iHeader ), m_group( iGroup )
{
    ABCA_ASSERT( m_parent, "Invalid parent" );
    ABCA_ASSERT( m_header, "Invalid property header" );
    ABCA_ASSERT( m_group, "Invalid group" );

    if ( m_header->header.getPropertyType() != AbcA::kArrayProperty )
    {
        ABCA_THROW( "Attempted to create a ArrayPropertyWriter from a "
                    "non-array property type" );
    }
}


//-*****************************************************************************
ApwImpl::~ApwImpl()
{
    AbcA::ArchiveWriterPtr archive = m_parent->getObject()->getArchive();

    index_t maxSamples = archive->getMaxNumSamplesForTimeSamplingIndex(
            m_header->timeSamplingIndex );

        uint32_t numSamples = m_header->nextSampleIndex;

        // a constant property, we wrote the same sample over and over
        if ( m_header->lastChangedIndex == 0 && m_header->nextSampleIndex > 0 )
        {
            numSamples = 1;
        }

        if ( maxSamples < numSamples )
        {
            archive->setMaxNumSamplesForTimeSamplingIndex(
                m_header->timeSamplingIndex, numSamples );
        }
}

//-*****************************************************************************
void ApwImpl::setFromPreviousSample()
{

    // Make sure we aren't writing more samples than we have times for
    // This applies to acyclic sampling only
    ABCA_ASSERT(
        !m_header->header.getTimeSampling()->getTimeSamplingType().isAcyclic()
        || m_header->header.getTimeSampling()->getNumStoredTimes() >
        m_header->nextSampleIndex,
        "Can not set more samples than we have times for when using "
        "Acyclic sampling." );

    ABCA_ASSERT( m_header->nextSampleIndex > 0,
        "Can't set from previous sample before any samples have been written" );

    m_header->nextSampleIndex ++;
}

//-*****************************************************************************
void ApwImpl::setSample( const AbcA::ArraySample & iSamp )
{
    // Make sure we aren't writing more samples than we have times for
    // This applies to acyclic sampling only
    ABCA_ASSERT(
        !m_header->header.getTimeSampling()->getTimeSamplingType().isAcyclic()
        || m_header->header.getTimeSampling()->getNumStoredTimes() >
        m_header->nextSampleIndex,
        "Can not write more samples than we have times for when using "
        "Acyclic sampling." );

    ABCA_ASSERT( iSamp.getDataType() == m_header->header.getDataType(),
        "DataType on ArraySample iSamp: " << iSamp.getDataType() <<
        ", does not match the DataType of the Array property: " <<
        m_header->header.getDataType() );

    // The Key helps us analyze the sample.
     AbcA::ArraySample::Key key = iSamp.getKey();

     // mask out the POD since Ogawa can safely share the same data even if
     // it originated from a different POD
     key.origPOD = Alembic::Util::kInt8POD;
     key.readPOD = Alembic::Util::kInt8POD;

    // We need to write the sample
    if ( m_header->nextSampleIndex == 0  ||
         !( m_previousWrittenSampleID &&
            key == m_previousWrittenSampleID->getKey() ) )
    {

        // we only need to repeat samples if this is not the first change
        if (m_header->firstChangedIndex != 0)
        {
            // copy the samples from after the last change to the latest index
            for ( index_t smpI = m_header->lastChangedIndex + 1;
                smpI < m_header->nextSampleIndex; ++smpI )
            {
                assert( smpI > 0 );
                CopyWrittenData( m_group, m_previousWrittenSampleID );
                WriteDimensions(m_group, iSamp);
            }
        }

        // Write this sample, which will update its internal
        // cache of what the previously written sample was.
        AbcA::ArchiveWriterPtr awp = this->getObject()->getArchive();

        // Write the sample.
        // This distinguishes between string, wstring, and regular arrays.
        m_previousWrittenSampleID =
            WriteData( GetWrittenSampleMap( awp ), m_group, iSamp, key );

        WriteDimensions(m_group, iSamp);

        // if we haven't written this already, isScalarLike will be true
        if ( m_header->isScalarLike && iSamp.getDimensions().numPoints() != 1 )
        {
            m_header->isScalarLike = false;
        }

        if ( m_header->isHomogenous && m_previousWrittenSampleID &&
            iSamp.getDimensions().numPoints() !=
            m_previousWrittenSampleID->getNumPoints() )
        {
            m_header->isHomogenous = false;
        }

        if (m_header->firstChangedIndex == 0)
        {
            m_header->firstChangedIndex = m_header->nextSampleIndex;
        }

        // this index is now the last change
        m_header->lastChangedIndex = m_header->nextSampleIndex;
    }

    m_header->nextSampleIndex ++;
}

//-*****************************************************************************
AbcA::ArrayPropertyWriterPtr ApwImpl::asArrayPtr()
{
    return shared_from_this();
}

//-*****************************************************************************
size_t ApwImpl::getNumSamples()
{
    return ( size_t )m_header->nextSampleIndex;
}

//-*****************************************************************************
void ApwImpl::setTimeSamplingIndex( uint32_t iIndex )
{
    // will assert if TimeSamplingPtr not found
    AbcA::TimeSamplingPtr ts =
        m_parent->getObject()->getArchive()->getTimeSampling(
            iIndex );

    ABCA_ASSERT( !ts->getTimeSamplingType().isAcyclic() ||
        ts->getNumStoredTimes() >= m_header->nextSampleIndex,
        "Already have written more samples than we have times for when using "
        "Acyclic sampling." );

    m_header->header.setTimeSampling(ts);
    m_header->timeSamplingIndex = iIndex;
}

//-*****************************************************************************
const AbcA::PropertyHeader & ApwImpl::getHeader() const
{
    ABCA_ASSERT( m_header, "Invalid header" );
    return m_header->header;
}

//-*****************************************************************************
AbcA::ObjectWriterPtr ApwImpl::getObject()
{
    ABCA_ASSERT( m_parent, "Invalid parent" );
    return m_parent->getObject();
}

//-*****************************************************************************
AbcA::CompoundPropertyWriterPtr ApwImpl::getParent()
{
    ABCA_ASSERT( m_parent, "Invalid parent" );
    return m_parent;
}

} // End namespace ALEMBIC_VERSION_NS
} // End namespace AbcCoreOgawa
} // End namespace Alembic
