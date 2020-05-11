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

#include "vtk_glew.h"
#include <chrono>

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
  GLenum Type;
  GLuint Handle;
};

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
    glDeleteBuffers(1, &this->Internal->Handle);
  }
  delete this->Internal;
}

void vtkOpenGLBufferObject::ReleaseGraphicsResources()
{
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
  if (!this->Internal->Handle)
  {
    return false;
  }

  glBindBuffer(this->Internal->Type, 0);
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

bool vtkOpenGLBufferObject::UploadInternal(
  const void* buffer, size_t size, vtkOpenGLBufferObject::ObjectType objectType)
{
	auto start = Timestamp();
  const bool generated = this->GenerateBuffer(objectType);
  if (!generated)
  {
    this->Error = "Trying to upload array buffer to incompatible buffer.";
    return false;
  }

  std::cout << "vtkOpenGLBufferObject::UploadInternal size " << size << " type " << this->Internal->Type << " handle " << this->Internal->Handle << std::endl;
  glBindBuffer(this->Internal->Type, this->Internal->Handle);
  // glBufferData(this->Internal->Type, size, static_cast<const GLvoid*>(buffer), GL_STATIC_DRAW);

  const auto* charptr = (const char*) buffer;
  glBufferData(this->Internal->Type, size, 0, GL_STATIC_DRAW);
  auto* p = glMapBufferRange(this->Internal->Type, 0,size, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
  auto* ptr = static_cast<char*>(p);
  if (!ptr) {
    std::cout << "ERROR in glMapBufferRange" << std::endl;
    GLenum err;
    while((err = glGetError()) != GL_NO_ERROR)
    {
      std::cout << "GL ERROR " << err << std::endl;
      std::cout << "  " << gl_error_string(err) << std::endl;
      std::cout << "gl buffer size " << gl_error_string(err) << std::endl;
    }
  }
#pragma omp parallel for
  for (int i = 0; i < size; ++i) {
    ptr[i] = charptr[i];
  }
  glUnmapBuffer(this->Internal->Type);

  auto duration = Timestamp() - start;
	double rate = static_cast<double>(size) / (1024 * 1024 * 1024) / duration * 1000.0;
	std::cout << "  transfer time " << duration << " rate: " << rate << "GB/s" << std::endl;
  this->Dirty = false;
  return true;
}

//-----------------------------------------------------------------------------
void vtkOpenGLBufferObject::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}
