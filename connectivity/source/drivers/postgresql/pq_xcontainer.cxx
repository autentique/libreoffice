/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*************************************************************************
 *
 *  Effective License of whole file:
 *
 *    This library is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License version 2.1, as published by the Free Software Foundation.
 *
 *    This library is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with this library; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *    MA  02111-1307  USA
 *
 *  Parts "Copyright by Sun Microsystems, Inc" prior to August 2011:
 *
 *    The Contents of this file are made available subject to the terms of
 *    the GNU Lesser General Public License Version 2.1
 *
 *    Copyright: 2000 by Sun Microsystems, Inc.
 *
 *    Contributor(s): Joerg Budischewski
 *
 *  All parts contributed on or after August 2011:
 *
 *    This Source Code Form is subject to the terms of the Mozilla Public
 *    License, v. 2.0. If a copy of the MPL was not distributed with this
 *    file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 ************************************************************************/

#include <com/sun/star/container/ElementExistException.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <comphelper/diagnose_ex.hxx>
#include <cppuhelper/implbase.hxx>
#include <o3tl/safeint.hxx>
#include <utility>

#include "pq_xcontainer.hxx"
#include "pq_statics.hxx"
#include "pq_tools.hxx"

using osl::MutexGuard;

using com::sun::star::beans::XPropertySet;

using com::sun::star::uno::Any;
using com::sun::star::uno::Type;
using com::sun::star::uno::XInterface;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
using com::sun::star::uno::RuntimeException;

using com::sun::star::container::NoSuchElementException;
using com::sun::star::container::XEnumeration;
using com::sun::star::container::XContainerListener;
using com::sun::star::container::ContainerEvent;
using com::sun::star::lang::IndexOutOfBoundsException;
using com::sun::star::lang::XEventListener;


namespace pq_sdbc_driver
{

namespace {

class ReplacedBroadcaster : public EventBroadcastHelper
{
    ContainerEvent m_event;
public:
    ReplacedBroadcaster(
        const Reference< XInterface > & source,
        const OUString & name,
        const Any & newElement,
        const OUString & oldElement ) :
        m_event( source, Any( name ), newElement, Any(oldElement) )
    {}

    virtual void fire( XEventListener * listener ) const override
    {
        static_cast<XContainerListener*>(listener)->elementReplaced( m_event );
    }
    virtual Type getType() const override
    {
        return cppu::UnoType<XContainerListener>::get();
    }
};

class InsertedBroadcaster : public EventBroadcastHelper
{
public:
    ContainerEvent m_event;
    InsertedBroadcaster(
        const Reference< XInterface > & source,
        const OUString & name,
        const Any & newElement ) :
        m_event( source, Any( name ), newElement, Any() )
    {}

    virtual void fire( XEventListener * listener ) const override
    {
        static_cast<XContainerListener*>(listener)->elementInserted( m_event );
    }

    virtual Type getType() const override
    {
        return cppu::UnoType<XContainerListener>::get();
    }
};

class RemovedBroadcaster : public EventBroadcastHelper
{
public:
    ContainerEvent m_event;
    RemovedBroadcaster(
        const Reference< XInterface > & source,
        const OUString & name) :
        m_event( source, Any( name ), Any(), Any() )
    {}

    virtual void fire( XEventListener * listener ) const override
    {
        static_cast<XContainerListener*>(listener)->elementRemoved( m_event );
    }

    virtual Type getType() const override
    {
        return cppu::UnoType<XContainerListener>::get();
    }
};

}

Container::Container(
    const ::rtl::Reference< comphelper::RefCountedMutex > & refMutex,
    css::uno::Reference< css::sdbc::XConnection > origin,
    ConnectionSettings *pSettings,
    OUString type)
    : ContainerBase( refMutex->GetMutex() ),
      m_xMutex( refMutex ),
      m_pSettings( pSettings ),
      m_origin(std::move( origin )),
      m_type(std::move( type ))
{
}

Any Container::getByName( const OUString& aName )
{
    String2IntMap::const_iterator ii = m_name2index.find( aName );
    if( ii == m_name2index.end() )
    {
        throw NoSuchElementException(
            "Element " + aName + " unknown in " + m_type + "-Container",
            *this );
    }
    OSL_ASSERT( ii->second >= 0 && o3tl::make_unsigned(ii->second) < m_values.size() );
    return m_values[ ii->second ];
}

Sequence< OUString > Container::getElementNames(  )
{
    Sequence< OUString > ret( m_values.size() );
    auto retRange = asNonConstRange(ret);
    for( const auto& [rName, rIndex] : m_name2index )
    {
        // give element names in index order !
        retRange[rIndex] = rName;
    }
    return ret;
}

sal_Bool Container::hasByName( const OUString& aName )
{
    return m_name2index.find( aName ) != m_name2index.end();
}
    // Methods
Type Container::getElementType(  )
{
    return Type();
}

sal_Bool Container::hasElements(  )
{
    return ! m_name2index.empty();
}

Any Container::getByIndex( sal_Int32 Index )
{
    if( Index < 0 || o3tl::make_unsigned(Index) >= m_values.size() )
    {
        throw IndexOutOfBoundsException(
            "Index " + OUString::number( Index )
            + " out of range for " + m_type + "-Container, expected 0 <= x <= "
            + OUString::number(m_values.size() -1),
            *this );
    }
    return m_values[Index];
}

sal_Int32 Container::getCount()
{
    return m_values.size();
}

namespace {

class ContainerEnumeration : public ::cppu::WeakImplHelper< XEnumeration >
{
    std::vector< css::uno::Any > m_vec;
    sal_Int32 m_index;
public:
    explicit ContainerEnumeration( std::vector< css::uno::Any >&& vec )
        : m_vec( std::move(vec) ),
          m_index( -1 )
    {}

public:
    // XEnumeration
    virtual sal_Bool SAL_CALL hasMoreElements(  ) override;
    virtual css::uno::Any SAL_CALL nextElement(  ) override;

};

}

sal_Bool ContainerEnumeration::hasMoreElements()
{
    return static_cast<int>(m_vec.size()) > m_index +1;
}

css::uno::Any ContainerEnumeration::nextElement()
{
    if( ! hasMoreElements() )
    {
        throw NoSuchElementException(
            "NoSuchElementException during enumeration", *this );
    }
    m_index ++;
    return m_vec[m_index];
}

Reference< XEnumeration > Container::createEnumeration(  )
{
    return new ContainerEnumeration( std::vector(m_values) );
}

void Container::addRefreshListener(
    const css::uno::Reference< css::util::XRefreshListener >& l )
{
    rBHelper.addListener( cppu::UnoType<decltype(l)>::get() , l );
}

void Container::removeRefreshListener(
    const css::uno::Reference< css::util::XRefreshListener >& l )
{
    rBHelper.removeListener( cppu::UnoType<decltype(l)>::get() , l );
}

void Container::disposing()
{
    m_origin.clear();
}

void Container::rename( const OUString &oldName, const OUString &newName )
{
    Any newValue;
    {
        osl::MutexGuard guard ( m_xMutex->GetMutex() );
        String2IntMap::iterator ii = m_name2index.find( oldName );
        if( ii != m_name2index.end() )
        {
            sal_Int32 nIndex = ii->second;
            newValue = m_values[nIndex];
            m_name2index.erase( ii );
            m_name2index[ newName ] = nIndex;
        }
    }
    fire( ReplacedBroadcaster( *this, newName, newValue, oldName ) );
    fire( RefreshedBroadcaster( *this ) );
}

void Container::dropByName( const OUString& elementName )
{
    osl::MutexGuard guard( m_xMutex->GetMutex() );
    String2IntMap::const_iterator ii = m_name2index.find( elementName );
    if( ii == m_name2index.end() )
    {
        throw css::container::NoSuchElementException(
            "Column " + elementName + " is unknown in "
            + m_type + " container, so it can't be dropped",
            *this );
    }
    dropByIndex( ii->second );
}

void Container::dropByIndex( sal_Int32 index )
{
    osl::MutexGuard guard( m_xMutex->GetMutex() );
    if( index < 0 ||  o3tl::make_unsigned(index) >=m_values.size() )
    {
        throw css::lang::IndexOutOfBoundsException(
            "Index out of range (allowed 0 to "
            + OUString::number(m_values.size() -1)
            + ", got " + OUString::number( index )
            + ") in " + m_type,
            *this );
    }

    OUString name;
    String2IntMap::iterator ii = std::find_if(m_name2index.begin(), m_name2index.end(),
        [&index](const String2IntMap::value_type& rEntry) { return rEntry.second == index; });
    if (ii != m_name2index.end())
    {
        name = ii->first;
        m_name2index.erase( ii );
    }

    for( int i = index +1 ; i < static_cast<int>(m_values.size()) ; i ++ )
    {
        m_values[i-1] = m_values[i];

        // I know, this is expensive, but don't want to maintain another map ...
        ii = std::find_if(m_name2index.begin(), m_name2index.end(),
            [&i](const String2IntMap::value_type& rEntry) { return rEntry.second == i; });
        if (ii != m_name2index.end())
        {
            ii->second = i-1;
        }
    }
    m_values.resize( m_values.size() - 1 );

    fire( RemovedBroadcaster( *this, name ) );
}

void Container::append(
    const OUString & name,
    const css::uno::Reference< css::beans::XPropertySet >& descriptor )

{
    osl::MutexGuard guard( m_xMutex->GetMutex() );

    if( hasByName( name ) )
    {
        throw css::container::ElementExistException(
            "a " + m_type + " with name " + name + " already exists in this container",
            *this );
    }

    int index = m_values.size();
    m_values.push_back( Any( descriptor ) );
    m_name2index[name] = index;

    fire( InsertedBroadcaster( *this, name, Any( descriptor ) ) );
}

void Container::appendByDescriptor(
    const css::uno::Reference< css::beans::XPropertySet >& descriptor)
{
    append( extractStringProperty( descriptor, getStatics().NAME ), descriptor );
}


void Container::addContainerListener(
        const css::uno::Reference< css::container::XContainerListener >& l )
{
    rBHelper.addListener( cppu::UnoType<decltype(l)>::get() , l );
}

void Container::removeContainerListener(
        const css::uno::Reference< css::container::XContainerListener >& l )
{
    rBHelper.removeListener( cppu::UnoType<decltype(l)>::get() , l );
}


void Container::fire( const EventBroadcastHelper &helper )
{
    cppu::OInterfaceContainerHelper *container = rBHelper.getContainer( helper.getType() );
    if( !container )
        return;

    cppu::OInterfaceIteratorHelper iterator( * container );
    while( iterator.hasMoreElements() )
    {
        try
        {
            helper.fire( static_cast<XEventListener *>(iterator.next()) );
        }
        catch ( css::uno::RuntimeException & )
        {
            TOOLS_WARN_EXCEPTION( "connectivity.postgresql", "exception caught" );
            // loose coupling, a runtime exception shall not break anything
            // TODO: log away as warning !
        }
        catch( css::uno::Exception & )
        {
            TOOLS_WARN_EXCEPTION( "connectivity.postgresql", "exception from listener flying through" );
            throw;
        }
    }

}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
