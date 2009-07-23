#include <memory.h>
#include <cuda_runtime.h>
#include <osgCuda/Context>
#include <osgCuda/Array>

namespace osgCuda
{
	/////////////////////////////////////////////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS /////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	//------------------------------------------------------------------------------
	ArrayStream::ArrayStream()
		:   osgCompute::BufferStream(),
			_devArray(NULL),
			_hostPtr(NULL),
			_syncDevice(false),
			_syncHost(false),
			_allocHint(0),
			_devArrayAllocated(false),
			_hostPtrAllocated(false),
			_modifyCount(UINT_MAX)
	{
	}

	//------------------------------------------------------------------------------
	ArrayStream::~ArrayStream()
	{
		if( _devArrayAllocated && NULL != _devArray )
			static_cast<Context*>(osgCompute::BufferStream::_context.get())->freeMemory( _devArray );

		if( _hostPtrAllocated && NULL != _hostPtr)
			static_cast<Context*>(osgCompute::BufferStream::_context.get())->freeMemory( _hostPtr );
	}


	/////////////////////////////////////////////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS /////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	//------------------------------------------------------------------------------
	Array::Array()
		: osgCompute::Buffer(),
		  osg::Object()
	{
		clearLocal();
	}

	//------------------------------------------------------------------------------
	void Array::clear()
	{
		clearLocal();
		osgCompute::Buffer::clear();
	}

	//------------------------------------------------------------------------------
	bool Array::init()
	{
		if( osgCompute::Buffer::getNumDimensions() > 3 )
		{
			osg::notify(osg::FATAL)
				<< "osgCuda::Array::init() for array \""
				<< osg::Object::getName() <<"\": the maximum dimension allowed is 3."
				<< std::endl;

			clear();
			return false;
		}

		unsigned int numElements = 1;
		for( unsigned int d=0; d<osgCompute::Buffer::getNumDimensions(); ++d )
			numElements *= osgCompute::Buffer::getDimension( d );

		unsigned int byteSize = numElements * getElementSize();

		// check stream data
		if( _image.valid() )
		{
			if( _image->getNumMipmapLevels() > 1 )
			{
				osg::notify(osg::FATAL)
					<< "osgCuda::Array::init() for array \""
					<< osg::Object::getName() <<"\": image \""
					<< _image->getName() << "\" uses MipMaps which are currently"
					<< "not supported."
					<< std::endl;

				clear();
				return false;
			}

			if( _image->getTotalSizeInBytes() != byteSize )
			{
				osg::notify(osg::FATAL)
					<< "osgCuda::Array::init() for array \""
					<< osg::Object::getName() <<"\": size of image \""
					<< _image->getName() << "\" does not match the array size."
					<< std::endl;

				clear();
				return false;
			}
		}

		if( _array.valid() )
		{
			if( _array->getTotalDataSize() != byteSize )
			{
				osg::notify(osg::FATAL)
					<< "osgCuda::Array::init() for array \""
					<< osg::Object::getName() <<"\": size of array \""
					<< _array->getName() << "\" is wrong."
					<< std::endl;

				clear();
				return false;
			}
		}

		return osgCompute::Buffer::init();
	}

	//------------------------------------------------------------------------------
	cudaArray* osgCuda::Array::mapArray( const osgCompute::Context& context, unsigned int mapping ) const
	{
		if( osgCompute::Resource::isClear() )
		{
			osg::notify(osg::INFO)
				<< "osgCuda::Array::map() for array \""
				<< osg::Object::getName() <<"\": array is dirty."
				<< std::endl;

			return NULL;
		}

		if( static_cast<const Context*>(&context)->getAssignedThread() != OpenThreads::Thread::CurrentThread() )
		{
			osg::notify(osg::FATAL)
				<< "osgCuda::Array::map() for array \""
				<< osg::Object::getName() <<"\": calling thread differs from the context's thread."
				<< std::endl;

			return NULL;
		}

		if( mapping & osgCompute::MAP_HOST )
		{
			osg::notify(osg::FATAL)
				<< "osgCuda::Array::mapArray() for array \""
				<< osg::Object::getName() <<"\": cannot map array to host. Call map() instead."
				<< std::endl;

			return NULL;
		}

		ArrayStream* stream = static_cast<ArrayStream*>( osgCompute::Buffer::lookupStream(context) );
		if( NULL == stream )
		{
			osg::notify(osg::FATAL)
				<< "osgCuda::Array::map() for array \""
				<< osg::Object::getName() <<"\": cannot receive ArrayStream for context \""
				<< context.getId() << "\"."
				<< std::endl;
			return NULL;
		}

		cudaArray* ptr = NULL;
		if( mapping != osgCompute::UNMAPPED )
			ptr = mapArrayStream( *stream, mapping );
		else
			unmapStream( *stream );

		return ptr;
	}

	//------------------------------------------------------------------------------
	void* Array::map( const osgCompute::Context& context, unsigned int mapping ) const
	{
		if( osgCompute::Resource::isClear() )
		{
			osg::notify(osg::FATAL)
				<< "osgCuda::Array::map() for array \""
				<< osg::Object::getName() <<"\": array is dirty."
				<< std::endl;

			return NULL;
		}

		if( static_cast<const Context*>(&context)->getAssignedThread() != OpenThreads::Thread::CurrentThread() )
		{
			osg::notify(osg::FATAL)
				<< "osgCuda::Array::map() for array \""
				<< osg::Object::getName() <<"\": calling thread differs from the context assigned thread."
				<< std::endl;

			return NULL;
		}

		ArrayStream* stream = static_cast<ArrayStream*>( osgCompute::Buffer::lookupStream(context) );
		if( NULL == stream )
		{
			osg::notify(osg::FATAL)
				<< "osgCuda::Array::map() for array \""
				<< osg::Object::getName() <<"\": cannot receive ArrayStream for context \""
				<< context.getId() << "\"."
				<< std::endl;
			return NULL;
		}

		if( mapping & osgCompute::MAP_DEVICE ) // lets try to avoid this in the future release
			return mapArrayStream( *stream, mapping );
		else if( mapping & osgCompute::MAP_HOST )
			return mapStream( *stream, mapping );
		else
			unmapStream( *stream );

		return NULL;
	}

	//------------------------------------------------------------------------------
	void Array::unmap( const osgCompute::Context& context ) const
	{
		if( osgCompute::Resource::isClear() )
		{
			osg::notify(osg::FATAL)
				<< "osgCuda::Array::map() for array \""
				<< osg::Object::getName() <<"\": array is dirty."
				<< std::endl;

			return;
		}

		if( static_cast<const Context*>(&context)->getAssignedThread() != OpenThreads::Thread::CurrentThread() )
		{
			osg::notify(osg::FATAL)
				<< "osgCuda::Array::map() for array \""
				<< osg::Object::getName() <<"\": calling thread differs from the context's thread."
				<< std::endl;

			return;
		}

		ArrayStream* stream = static_cast<ArrayStream*>( osgCompute::Buffer::lookupStream(context) );
		if( NULL == stream )
		{
			osg::notify(osg::FATAL)
				<< "osgCuda::Array::map() for array \""
				<< osg::Object::getName() <<"\": could not receive ArrayStream for context \""
				<< context.getId() << "\"."
				<< std::endl;

			return;
		}

		unmapStream( *stream );
	}

	//------------------------------------------------------------------------------
	bool osgCuda::Array::setMemory( const osgCompute::Context& context, int value, unsigned int mapping, unsigned int offset, unsigned int count ) const
	{
		unsigned char* data = static_cast<unsigned char*>( map( context, mapping ) );
		if( NULL == data )
			return false;

		if( mapping & osgCompute::MAP_HOST_TARGET )
		{
			if( NULL == memset( &data[offset], value, (count == UINT_MAX)? getByteSize() : count ) )
			{
				osg::notify(osg::FATAL)
					<< "osgCuda::Array::setMemory() for array \""
					<< osg::Object::getName() <<"\": error during memset() for host within context \""
					<< context.getId() << "\"."
					<< std::endl;

				unmap( context );
				return false;
			}

			return true;
		}
		else if( mapping & osgCompute::MAP_DEVICE_TARGET )
		{
			osg::notify(osg::INFO)
				<< "osgCuda::Array::setMemory() for array \""
				<< osg::Object::getName() <<"\": cudaMemset() for cuda arrays is not available yet."
				<< std::endl;

			return true;
		}

		unmap( context );
		return false;
	}

	//------------------------------------------------------------------------------
	cudaArray* Array::mapArrayStream( ArrayStream& stream, unsigned int mapping ) const
	{
		cudaArray* ptr = NULL;

		bool needsSetup = false;
		if( (_image.valid() && _image->getModifiedCount() != stream._modifyCount ) ||
			(_array.valid() && _array->getModifiedCount() != stream._modifyCount ) )
			needsSetup = true;

		///////////////////
		// PROOF MAPPING //
		///////////////////
		if( (stream._mapping & osgCompute::MAP_DEVICE && mapping & osgCompute::MAP_DEVICE) &&
			!needsSetup )
		{
			if( osgCompute::Buffer::getSubloadResourceCallback() && NULL != stream._devArray )
			{
				const osgCompute::BufferSubloadCallback* callback = 
					dynamic_cast<const osgCompute::BufferSubloadCallback*>(osgCompute::Buffer::getSubloadResourceCallback());
				if( callback )
				{
					// load or subload data before returning the host pointer
					callback->subload( reinterpret_cast<void*>(stream._devArray), mapping, *this, *stream._context );
				}
			}

			stream._mapping = mapping;
			return stream._devArray;
		}
		else if( stream._mapping != osgCompute::UNMAPPED )
		{
			unmapStream( stream );
		}

		stream._mapping = mapping;

		//////////////
		// MAP DATA //
		//////////////
		bool firstLoad = false;
		if( (stream._mapping & osgCompute::MAP_DEVICE) )
		{
			if( NULL == stream._devArray )
			{
				////////////////////////////
				// ALLOCATE DEVICE-MEMORY //
				////////////////////////////
				if( !allocStream( mapping, stream ) )
					return NULL;

				firstLoad = true;
			}


			//////////////////
			// SETUP STREAM //
			//////////////////
			if( needsSetup )
				if( !setupStream( mapping, stream ) )
					return NULL;

			/////////////////
			// SYNC STREAM //
			/////////////////
			if( stream._syncDevice && NULL != stream._hostPtr )
				if( !syncStream( mapping, stream ) )
					return NULL;

			ptr = stream._devArray;
		}
		else
		{
			osg::notify(osg::WARN)
				<< "osgCuda::Array::mapArrayStream() for array \""<<osg::Object::getName()<<"\": wrong mapping was specified. Use one of the following: "
				<< "DEVICE_SOURCE, DEVICE_TARGET, DEVICE."
				<< std::endl;

			return NULL;
		}

		//////////////////
		// LOAD/SUBLOAD //
		//////////////////
		if( osgCompute::Buffer::getSubloadResourceCallback() && NULL != ptr )
		{
			const osgCompute::BufferSubloadCallback* callback = 
				dynamic_cast<const osgCompute::BufferSubloadCallback*>( osgCompute::Buffer::getSubloadResourceCallback() );
			if( callback )
			{
				// load or subload data before returning the host pointer
				if( firstLoad )
					callback->load( reinterpret_cast<void*>(ptr), mapping, *this, *stream._context );
				else
					callback->subload( reinterpret_cast<void*>(ptr), mapping, *this, *stream._context );
			}
		}

		return ptr;
	}

	//------------------------------------------------------------------------------
	void* Array::mapStream( ArrayStream& stream, unsigned int mapping ) const
	{
		void* ptr = NULL;

		bool needsSetup = false;
		if( (_image.valid() && _image->getModifiedCount() != stream._modifyCount ) ||
			(_array.valid() && _array->getModifiedCount() != stream._modifyCount ) )
			needsSetup = true;

		///////////////////
		// PROOF MAPPING //
		///////////////////
		if( (stream._mapping & osgCompute::MAP_HOST && mapping & osgCompute::MAP_HOST) &&
			!needsSetup )
		{
			if( osgCompute::Buffer::getSubloadResourceCallback() && NULL != stream._hostPtr )
			{
				const osgCompute::BufferSubloadCallback* callback = 
					dynamic_cast<const osgCompute::BufferSubloadCallback*>( osgCompute::Buffer::getSubloadResourceCallback() );
				if( callback )
				{
					// subload data before returning the pointer
					callback->subload( stream._hostPtr, mapping, *this, *stream._context );
				}
			}

			stream._mapping = mapping;
			return stream._hostPtr;
		}
		else if( stream._mapping != osgCompute::UNMAPPED )
		{
			unmapStream( stream );
		}

		stream._mapping = mapping;

		//////////////
		// MAP DATA //
		//////////////
		bool firstLoad = false;
		if( (stream._mapping & osgCompute::MAP_HOST) )
		{
			if( NULL == stream._hostPtr )
			{
				//////////////////////////
				// ALLOCATE HOST-MEMORY //
				//////////////////////////
				if( !allocStream( mapping, stream ) )
					return NULL;

				firstLoad = true;
			}

			//////////////////
			// SETUP STREAM //
			//////////////////
			if( needsSetup )
				if( !setupStream( mapping, stream ) )
					return NULL;

			/////////////////
			// SYNC STREAM //
			/////////////////
			if( stream._syncHost && NULL != stream._devArray )
				if( !syncStream( mapping, stream ) )
					return NULL;

			ptr = stream._hostPtr;
		}
		else
		{
			osg::notify(osg::WARN)
				<< "osgCuda::Array::mapStream() for array \""<<osg::Object::getName()<<"\": wrong mapping specified. Use one of the following: "
				<< "HOST_SOURCE, HOST_TARGET, HOST."
				<< std::endl;

			return NULL;
		}

		//////////////////
		// LOAD/SUBLOAD //
		//////////////////
		if( osgCompute::Buffer::getSubloadResourceCallback() && NULL != ptr )
		{
			const osgCompute::BufferSubloadCallback* callback = 
				dynamic_cast<const osgCompute::BufferSubloadCallback*>( osgCompute::Buffer::getSubloadResourceCallback() );
			if( callback )
			{
				// load or subload data before returning the host pointer
				if( firstLoad )
					callback->load( ptr, mapping, *this, *stream._context );
				else
					callback->subload( ptr, mapping, *this, *stream._context );
			}
		}

		return ptr;
	}

	//------------------------------------------------------------------------------
	bool Array::setupStream( unsigned int mapping, ArrayStream& stream ) const
	{
		cudaError res;

		if( mapping & osgCompute::MAP_DEVICE )
		{
			const void* data = NULL;
			if( _image.valid() )
			{
				data = _image->data();
			}

			if( _array.valid() )
			{
				data = reinterpret_cast<const void*>(_array->getDataPointer());
			}

			if( data == NULL )
			{
				osg::notify(osg::FATAL)
					<< "osgCuda::Array::setupStream() for array \""<< osg::Object::getName()
					<< "\": Cannot receive valid data pointer."
					<< std::endl;

				return false;
			}

			if( getNumDimensions() < 3 )
			{
				res = cudaMemcpyToArray(stream._devArray,0,0,data, osgCompute::Buffer::getByteSize(), cudaMemcpyHostToDevice);
				if( cudaSuccess != res )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::setupStream() for array \""<< osg::Object::getName()
						<< "\": cudaMemcpyToArray() failed for data within context \""
						<< stream._context->getId() << "\"." 
						<< " " << cudaGetErrorString( res ) << "."
						<< std::endl;

					return false;
				}
			}
			else
			{
				cudaMemcpy3DParms memCpyParams = {0};
				memCpyParams.dstArray = stream._devArray;
				memCpyParams.kind = cudaMemcpyHostToDevice;
				memCpyParams.srcPtr = make_cudaPitchedPtr((void*)data, getDimension(0)*getElementSize(), getDimension(0), getDimension(1));

				cudaExtent arrayExtent = {0};
				arrayExtent.width = getDimension(0);
				arrayExtent.height = getDimension(1);
				arrayExtent.depth = getDimension(2);

				memCpyParams.extent = arrayExtent;

				res = cudaMemcpy3D( &memCpyParams );
				if( cudaSuccess != res )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::setupStream() for array \""<< osg::Object::getName()
						<< "\": cudaMemcpy3D() failed for data within context \""
						<< stream._context->getId() << "\"." 
						<< " " << cudaGetErrorString( res ) <<"."
						<< std::endl;

					return false;
				}
			}


			// host must be synchronized
			stream._syncHost = true;
			stream._modifyCount = _image.valid()? _image->getModifiedCount() : _array->getModifiedCount();
			return true;
		}
		else if( mapping & osgCompute::MAP_HOST )
		{
			const void* data = NULL;
			if( _image.valid() )
			{
				data = _image->data();
			}

			if( _array.valid() )
			{
				data = reinterpret_cast<const void*>( _array->getDataPointer() );
			}

			if( data == NULL )
			{
				osg::notify(osg::FATAL)
					<< "osgCuda::Array::setupStream() for array \""<< osg::Object::getName()
					<< "\": Cannot receive valid data pointer."
					<< std::endl;

				return false;
			}

			res = cudaMemcpy( stream._hostPtr, data, osgCompute::Buffer::getByteSize(), cudaMemcpyHostToHost );
			if( cudaSuccess != res )
			{
				osg::notify(osg::FATAL)
					<< "osgCuda::Array::setupStream() for array \""<< osg::Object::getName()
					<< "\": error during cudaMemcpy() within context \""
					<< stream._context->getId() << "\"." 
					<< " " << cudaGetErrorString( res ) <<"."
					<< std::endl;

				return false;
			}

			// device must be synchronized
			stream._syncDevice = true;
			stream._modifyCount = _image.valid()? _image->getModifiedCount() : _array->getModifiedCount();
			return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------
	bool Array::allocStream( unsigned int mapping, ArrayStream& stream ) const
	{
		if( mapping & osgCompute::MAP_HOST )
		{
			if( stream._hostPtr != NULL )
				return true;

			if( (stream._allocHint & ALLOC_DYNAMIC) == ALLOC_DYNAMIC )
			{
				stream._hostPtr = static_cast<Context*>(stream._context.get())->mallocDeviceHostMemory( osgCompute::Buffer::getByteSize() );
				if( NULL == stream._hostPtr )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::allocStream() for array \""
						<< osg::Object::getName()<<"\": something goes wrong within mallocDeviceHostMemory() within Context \""<<stream._context->getId()
						<< "\"."
						<< std::endl;

					return false;
				}
			}
			else
			{
				stream._hostPtr = static_cast<Context*>(stream._context.get())->mallocHostMemory( osgCompute::Buffer::getByteSize() );
				if( NULL == stream._hostPtr )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::allocStream() for array \""
						<< osg::Object::getName()<<"\": something goes wrong within mallocHostMemory() within Context \""<<stream._context->getId()
						<< "\"."
						<< std::endl;

					return false;
				}
			}

			stream._hostPtrAllocated = true;
			if( stream._devArray != NULL )
				stream._syncHost = true;
			return true;
		}
		else if( mapping & osgCompute::MAP_DEVICE )
		{
			if( stream._devArray != NULL )
				return true;

			const cudaChannelFormatDesc& desc = getChannelFormatDesc();
			if( desc.x == INT_MAX && desc.y == INT_MAX && desc.z == INT_MAX )
			{
				osg::notify(osg::FATAL)
					<< "osgCuda::Array::allocStream() for array \""<<osg::Object::getName()<<"\": no valid ChannelFormatDesc found."
					<< std::endl;

				return false;
			}

			if( osgCompute::Buffer::getNumDimensions() == 3 )
			{
				stream._devArray = static_cast<Context*>(stream._context.get())->mallocDevice3DArray(
					osgCompute::Buffer::getDimension(0),
					(osgCompute::Buffer::getDimension(1) <= 1)? 0 : osgCompute::Buffer::getDimension(1),
					(osgCompute::Buffer::getDimension(2) <= 1)? 0 : osgCompute::Buffer::getDimension(2),
					desc );
				if( NULL == stream._devArray )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::allocStream() for array \""<<osg::Object::getName()<<"\": something goes wrong within mallocDevice3DArray() within context \""
						<< stream._context->getId() << "\"."
						<< std::endl;

					return false;
				}
			}
			else if( osgCompute::Buffer::getNumDimensions() == 2 )
			{
				stream._devArray = static_cast<Context*>(stream._context.get())->mallocDevice2DArray(
					osgCompute::Buffer::getDimension(0),
					(osgCompute::Buffer::getDimension(1) <= 1)? 0 : osgCompute::Buffer::getDimension(1),
					desc );
				if( NULL == stream._devArray )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::allocStream() for array \""<<osg::Object::getName()<<"\": something goes wrong within mallocDevice2DArray() within context \""
						<< stream._context->getId() << "\"."
						<< std::endl;

					return false;
				}
			}
			else 
			{
				stream._devArray = static_cast<Context*>(stream._context.get())->mallocDeviceArray(
					osgCompute::Buffer::getDimension(0),
					desc );
				if( NULL == stream._devArray )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::allocStream() for array \""<<osg::Object::getName()<<"\": something goes wrong within mallocDeviceArray() within context \""
						<< stream._context->getId() << "\"."
						<< std::endl;

					return false;
				}
			}

			stream._devArrayAllocated = true;
			if( stream._hostPtr != NULL )
				stream._syncDevice = true;
			return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------
	bool Array::syncStream( unsigned int mapping, ArrayStream& stream ) const
	{
		cudaError res;
		if( mapping & osgCompute::MAP_DEVICE )
		{
			if( osgCompute::Buffer::getNumDimensions() == 1 )
			{
				res = cudaMemcpyToArray( stream._devArray, 0, 0, stream._hostPtr, osgCompute::Buffer::getByteSize(), cudaMemcpyHostToDevice );
				if( cudaSuccess != res )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::syncStream() for array \""<<osg::Object::getName()
						<< "\": error during cudaMemcpyToArray() to device within context \""
						<< stream._context->getId() << "\"."
						<< " " << cudaGetErrorString( res )<<"."
						<< std::endl;
					return false;
				}
			}
			else if( osgCompute::Buffer::getNumDimensions() == 2 )
			{
				res = cudaMemcpy2DToArray( stream._devArray,
					0, 0,
					stream._hostPtr,
					osgCompute::Buffer::getDimension(0) * getElementSize(),
					osgCompute::Buffer::getDimension(0),
					osgCompute::Buffer::getDimension(1),
					cudaMemcpyHostToDevice );
				if( cudaSuccess != res )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::syncStream() for array \""<<osg::Object::getName()
						<< "\": error during cudaMemcpy2DToArray() to device within context \""
						<< stream._context->getId() << "\"."
						<< " " << cudaGetErrorString( res ) << "."
						<< std::endl;

					return false;
				}
			}
			else
			{
				cudaPitchedPtr pitchPtr = {0};
				pitchPtr.pitch = osgCompute::Buffer::getDimension(0) * getElementSize();
				pitchPtr.ptr = (void*)stream._hostPtr;
				pitchPtr.xsize = osgCompute::Buffer::getDimension(0);
				pitchPtr.ysize = osgCompute::Buffer::getDimension(1);

				cudaExtent extent = {0};
				extent.width = osgCompute::Buffer::getDimension(0);
				extent.height = osgCompute::Buffer::getDimension(1);
				extent.depth = osgCompute::Buffer::getDimension(2);

				cudaMemcpy3DParms copyParams = {0};
				copyParams.srcPtr = pitchPtr;
				copyParams.dstArray = stream._devArray;
				copyParams.extent = extent;
				copyParams.kind = cudaMemcpyHostToDevice;

				res = cudaMemcpy3D( &copyParams );
				if( cudaSuccess != res )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::syncStream() for array \""<<osg::Object::getName()
						<< "\": error during cudaMemcpy3D() to device within context \""
						<< stream._context->getId() << "\"."
						<< " " << cudaGetErrorString( res ) << "."
						<< std::endl;

					return false;
				}
			}

			stream._syncDevice = false;
			return true;
		}
		else if( mapping & osgCompute::MAP_HOST )
		{
			if( osgCompute::Buffer::getNumDimensions() == 1 )
			{
				res = cudaMemcpyFromArray( stream._hostPtr, stream._devArray, 0, 0, osgCompute::Buffer::getByteSize(), cudaMemcpyDeviceToHost );
				if( cudaSuccess != res )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::syncStream() for array \""
						<< osg::Object::getName()<<"\": something goes wrong within cudaMemcpyFromArray() to host within context \""
						<< stream._context->getId() << "\"."
						<< " " << cudaGetErrorString( res ) <<"."
						<< std::endl;

					return false;
				}
			}
			else if( osgCompute::Buffer::getNumDimensions() == 2 )
			{
				res = cudaMemcpy2DFromArray(
					stream._hostPtr,
					osgCompute::Buffer::getDimension(0) * getElementSize(),
					stream._devArray,
					0, 0,
					osgCompute::Buffer::getDimension(0),
					osgCompute::Buffer::getDimension(1),
					cudaMemcpyDeviceToHost );
				if( cudaSuccess != res )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::syncStream() for array \""<<osg::Object::getName()
						<< "\": error during cudaMemcpy2DFromArray() to device within context \""
						<< stream._context->getId() << "\"."
						<< " " << cudaGetErrorString( res ) <<"."
						<< std::endl;

					return false;
				}
			}
			else
			{
				cudaPitchedPtr pitchPtr = {0};
				pitchPtr.pitch = osgCompute::Buffer::getDimension(0)*getElementSize();
				pitchPtr.ptr = (void*)stream._hostPtr;
				pitchPtr.xsize = osgCompute::Buffer::getDimension(0);
				pitchPtr.ysize = osgCompute::Buffer::getDimension(1);

				cudaExtent extent = {0};
				extent.width = osgCompute::Buffer::getDimension(0);
				extent.height = osgCompute::Buffer::getDimension(1);
				extent.depth = osgCompute::Buffer::getDimension(2);

				cudaMemcpy3DParms copyParams = {0};
				copyParams.srcArray = stream._devArray;
				copyParams.dstPtr = pitchPtr;
				copyParams.extent = extent;
				copyParams.kind = cudaMemcpyDeviceToHost;

				res = cudaMemcpy3D( &copyParams );
				if( cudaSuccess != res )
				{
					osg::notify(osg::FATAL)
						<< "osgCuda::Array::syncStream() for array \""<<osg::Object::getName()
						<< "\": error during cudaMemcpy3D() to device within context \""
						<< stream._context->getId() << "\"."
						<< " " << cudaGetErrorString( res ) <<"."
						<< std::endl;

					return false;

				}
			}

			stream._syncHost = false;
			return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------
	void Array::unmapStream( ArrayStream& stream ) const
	{
		if( (stream._mapping & osgCompute::MAP_HOST_TARGET) )
		{
			stream._syncDevice = true;
		}
		else if( (stream._mapping & osgCompute::MAP_DEVICE_TARGET) )
		{
			stream._syncHost = true;
		}

		stream._mapping = osgCompute::UNMAPPED;
	}

	//------------------------------------------------------------------------------
	void Array::setImage( osg::Image* image )
	{
		if( !osgCompute::Resource::isClear() && NULL != image)
		{
			if( image->getNumMipmapLevels() > 1 )
			{
				osg::notify(osg::FATAL)
					<< "osgCuda::Array::setupStream() for array \""
					<< osg::Object::getName() <<"\": image \""
					<< image->getName() << "\" uses MipMaps which are currently"
					<< "not supported."
					<< std::endl;

				return;
			}

			if( image->getTotalSizeInBytes() != osgCompute::Buffer::getByteSize() )
			{
				osg::notify(osg::FATAL)
					<< "osgCuda::Array::setupStream() for array \""
					<< osg::Object::getName() <<"\": size of image \""
					<< image->getName() << "\" is wrong."
					<< std::endl;

				return;
			}
		}

		_image = image;
		_array = NULL;
	}

	//------------------------------------------------------------------------------
	osg::Image* Array::getImage()
	{
		return _image.get();
	}

	//------------------------------------------------------------------------------
	const osg::Image* Array::getImage() const
	{
		return _image.get();
	}

	//------------------------------------------------------------------------------
	void Array::setArray( osg::Array* array )
	{
		if( !osgCompute::Resource::isClear() && array != NULL )
		{
			if( array->getTotalDataSize() != osgCompute::Buffer::getByteSize() )
			{
				osg::notify(osg::FATAL)
					<< "osgCuda::Array::setArray() for buffer \""
					<< osg::Object::getName() <<"\": size of array \""
					<< array->getName() << "\" does not match with the array size."
					<< std::endl;

				return;
			}
		}

		_array = array;
		_image = NULL;
	}

	//------------------------------------------------------------------------------
	osg::Array* Array::getArray()
	{
		return _array.get();
	}

	//------------------------------------------------------------------------------
	const osg::Array* Array::getArray() const
	{
		return _array.get();
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// PROTECTED FUNCTIONS //////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	//------------------------------------------------------------------------------
	void Array::clearLocal()
	{
		_image = NULL;
		_array = NULL;
		memset( &_channelFormatDesc, INT_MAX, sizeof(cudaChannelFormatDesc) );
	}

	//------------------------------------------------------------------------------
	osgCompute::BufferStream* Array::newStream( const osgCompute::Context& context ) const
	{
		return new ArrayStream;
	}

}