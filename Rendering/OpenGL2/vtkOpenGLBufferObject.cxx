/*=========================================================================

  Program:   Visualization Toolkit

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkOpenGLBufferObject.h"
#include "vtkObjectFactory.h"
#include "vtkCommand.h"

#include "vtk_glew.h"
#include <chrono>
#include <unistd.h> // FIXME remove

vtkStandardNewMacro(vtkOpenGLBufferObject);

namespace
{
inline GLenum convertType(vtkOpenGLBufferObject::ObjectType type)
{
  switch (type)
  {
    case vtkOpenGLBufferObject::ElementArrayBuffer:
      return GL_ELEMENT_ARRAY_BUFFER;
    case vtkOpenGLBufferObject::TextureBuffer:
#if defined(GL_TEXTURE_BUFFER)
      return GL_TEXTURE_BUFFER;
      // intentional fall through when not defined
#endif
    default:
    case vtkOpenGLBufferObject::ArrayBuffer:
      return GL_ARRAY_BUFFER;
  }
}
}

struct vtkOpenGLBufferObject::Private
{
  Private()
  {
    this->Handle = 0;
    this->Type = GL_ARRAY_BUFFER;
  }

  Private(const Private& other) : Type(other.Type), Handle(other.Handle), 
    Buffer(other.Buffer), Capacity(other.Capacity), 
    Sync(other.Sync) {}

  Private& operator=(const Private& other) {
    if(&other == this) {
      return *this;
    }
    Type = other.Type;
    Handle = other.Handle;
    Buffer = other.Buffer;
    Capacity = other.Capacity;
    Sync = other.Sync;
    return *this;
  }

  GLenum Type;
  GLuint Handle;
  void* Buffer = nullptr;
  size_t Capacity = 0;  
  GLsync Sync = 0;
};

std::vector<vtkOpenGLBufferObject::Private*> vtkOpenGLBufferObject::kCachedOGLData;

vtkOpenGLBufferObject::vtkOpenGLBufferObject()
{
  this->Dirty = true;
  this->Internal = new Private;
  this->Internal->Type = convertType(vtkOpenGLBufferObject::ArrayBuffer);
}

vtkOpenGLBufferObject::~vtkOpenGLBufferObject()
{
  if (this->Internal->Handle != 0)
  {
    // glDeleteBuffers(1, &this->Internal->Handle);
  }
  delete this->Internal;
}

void vtkOpenGLBufferObject::ReleaseGraphicsResources()
{
  std::cout << "ReleaseGraphicsResources" << std::endl;
  if (this->Internal->Handle != 0)
  {
    glBindBuffer(this->Internal->Type, 0);
    glDeleteBuffers(1, &this->Internal->Handle);
    this->Internal->Handle = 0;
  }
}

void vtkOpenGLBufferObject::SetType(vtkOpenGLBufferObject::ObjectType value)
{
  this->Internal->Type = convertType(value);
}

vtkOpenGLBufferObject::ObjectType vtkOpenGLBufferObject::GetType() const
{
  if (this->Internal->Type == GL_ARRAY_BUFFER)
  {
    return vtkOpenGLBufferObject::ArrayBuffer;
  }
  if (this->Internal->Type == GL_ELEMENT_ARRAY_BUFFER)
  {
    return vtkOpenGLBufferObject::ElementArrayBuffer;
  }
  else
  {
    return vtkOpenGLBufferObject::TextureBuffer;
  }
}

int vtkOpenGLBufferObject::GetHandle() const
{
  return static_cast<int>(this->Internal->Handle);
}

bool vtkOpenGLBufferObject::Bind()
{
  if (!this->Internal->Handle)
  {
    return false;
  }

  glBindBuffer(this->Internal->Type, this->Internal->Handle);
  return true;
}

bool vtkOpenGLBufferObject::Release()
{
  std::cout << "ReleaseBuffer" << std::endl;
  if (!this->Internal->Handle)
  {
    return false;
  }

  // glBindBuffer(this->Internal->Type, 0);
  return true;
}

bool vtkOpenGLBufferObject::GenerateBuffer(vtkOpenGLBufferObject::ObjectType objectType)
{
  GLenum objectTypeGL = convertType(objectType);
  if (this->Internal->Handle == 0)
  {
    glGenBuffers(1, &this->Internal->Handle);
    this->Internal->Type = objectTypeGL;
  }
  return (this->Internal->Type == objectTypeGL);
}

int64_t Timestamp() {
	using std::chrono::milliseconds;
	using std::chrono::duration_cast;
	auto time = std::chrono::high_resolution_clock::now();
	auto since_epoch = time.time_since_epoch();
	auto millis = duration_cast<milliseconds>(since_epoch);
	return millis.count();
}

char const* gl_error_string(GLenum const err) noexcept
{
  switch (err)
  {
    // opengl 2 errors (8)
    case GL_NO_ERROR:
      return "GL_NO_ERROR";

    case GL_INVALID_ENUM:
      return "GL_INVALID_ENUM";

    case GL_INVALID_VALUE:
      return "GL_INVALID_VALUE";

    case GL_INVALID_OPERATION:
      return "GL_INVALID_OPERATION";

    case GL_STACK_OVERFLOW:
      return "GL_STACK_OVERFLOW";

    case GL_STACK_UNDERFLOW:
      return "GL_STACK_UNDERFLOW";

    case GL_OUT_OF_MEMORY:
      return "GL_OUT_OF_MEMORY";

    case GL_TABLE_TOO_LARGE:
      return "GL_TABLE_TOO_LARGE";

    // opengl 3 errors (1)
    case GL_INVALID_FRAMEBUFFER_OPERATION:
      return "GL_INVALID_FRAMEBUFFER_OPERATION";

    // gles 2, 3 and gl 4 error are handled by the switch above
    default:
      return "unknown error";
  }
}

void LockBuffer(GLsync& syncObj)
{
  if (syncObj) {
    glDeleteSync(syncObj);
    syncObj = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  }
}

void WaitBuffer(GLsync& syncObj)
{
  if (syncObj)
  {
    while (1)
    {
      GLenum waitReturn = glClientWaitSync(syncObj, GL_SYNC_FLUSH_COMMANDS_BIT, 1);
      if (waitReturn == GL_ALREADY_SIGNALED || waitReturn == GL_CONDITION_SATISFIED)
      {
        return;
      }
    }
  }
}

bool vtkOpenGLBufferObject::UploadInternal(
  const void* buffer, size_t size, vtkOpenGLBufferObject::ObjectType objectType)
{
  auto old_handle = this->Internal->Handle;
  auto cache_idx = ++vtkCommand::kOpenGLCacheIndex;
  // restore
  if (old_handle == 0 && cache_idx < kCachedOGLData.size()) {
    *this->Internal = *kCachedOGLData[cache_idx-1];
    if (size <= this->Internal->Capacity) {
      old_handle = this->Internal->Handle;
    }
    // FIXME deallocate current ogl resources 
  }
  const bool generated = this->GenerateBuffer(objectType);
  if (!generated)
  {
    this->Error = "Trying to upload array buffer to incompatible buffer.";
    return false;
  }

  if (old_handle == 0) {
    std::cout << "old handle was 0" << std::endl;
    glBindBuffer(this->Internal->Type, this->Internal->Handle);
    glBufferStorage(this->Internal->Type, size,  0, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
    this->Internal->Buffer = glMapBufferRange(this->Internal->Type, 0, size, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
    if (!this->Internal->Buffer) {
      std::cout << "ERROR in glMapBufferRange" << std::endl;
      GLenum err;
      while((err = glGetError()) != GL_NO_ERROR)
      {
        std::cout << "GL ERROR " << err << std::endl;
        std::cout << "  " << gl_error_string(err) << std::endl;
        std::cout << "gl buffer size " << gl_error_string(err) << std::endl;
      }
    }
    this->Internal->Capacity = size;
  }
  const auto* charptr = (const uint64_t*) buffer;
  auto* ptr = static_cast<uint64_t*>(this->Internal->Buffer);

  WaitBuffer(this->Internal->Sync);
#pragma omp parallel for 
    for (int i = 0; i < size / sizeof(uint64_t); ++i) {
      ptr[i] = charptr[i];
    }
  LockBuffer(this->Internal->Sync);

  // backup 
  if (old_handle == 0 && cache_idx >= kCachedOGLData.size()) {
    kCachedOGLData.push_back(new Private(*this->Internal));
  }
  this->Dirty = false;
  return true;
}

//-----------------------------------------------------------------------------
void vtkOpenGLBufferObject::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}
