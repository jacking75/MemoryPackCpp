#pragma once
/// @file memorypack.hpp
/// @brief MemoryPack Binary Wire Format compatible C++ header-only library.
///        Interoperable with C# MemoryPack (https://github.com/Cysharp/MemoryPack).
///        Requires C++23 or later.
///
/// This umbrella header pulls in the whole library:
///
///   memorypack/core.hpp        writer/reader, primitives, strings, objects,
///                              collections, unions, unmanaged structs
///   memorypack/containers.hpp  std:: containers, optional, smart pointers,
///                              std::variant as a MemoryPack union
///   memorypack/dotnet.hpp      Guid, DateTime, TimeSpan, decimal, Half, Int128
///
/// memorypack/packet.hpp (TCP framing helpers) is optional and not included here.
///
/// Quick start:
///
///     struct LoginRequest { std::string userName; int32_t level; };
///     MEMORYPACK_DEFINE(LoginRequest, userName, level);
///
///     auto bytes = memorypack::Serialize(LoginRequest{"Player1", 42});
///     auto back  = memorypack::Deserialize<LoginRequest>(bytes);

#include "memorypack/core.hpp"
#include "memorypack/containers.hpp"
#include "memorypack/dotnet.hpp"
