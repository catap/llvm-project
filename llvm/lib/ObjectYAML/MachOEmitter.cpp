//===- yaml2macho - Convert YAML to a Mach object file --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The Mach component of yaml2obj.
///
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MachO.h"
#include "llvm/ObjectYAML/DWARFEmitter.h"
#include "llvm/ObjectYAML/ObjectYAML.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Support/Format.h"

#ifdef __APPLE__
#include <Availability.h>
#if __MAC_OS_X_VERSION_MIN_REQUIRED < 1070
static size_t strnlen(const char *s, size_t maxlen) {
  size_t l = 0;
  while (l < maxlen && *s) {
    l++;
    s++;
  }
  return l;
}
#endif
#endif

using namespace llvm;

namespace {

class MachOWriter {
public:
  MachOWriter(MachOYAML::Object &Obj) : Obj(Obj), is64Bit(true), fileStart(0) {
    is64Bit = Obj.Header.magic == MachO::MH_MAGIC_64 ||
              Obj.Header.magic == MachO::MH_CIGAM_64;
    memset(reinterpret_cast<void *>(&Header), 0, sizeof(MachO::mach_header_64));
  }

  Error writeMachO(raw_ostream &OS);

private:
  void writeHeader(raw_ostream &OS);
  void writeLoadCommands(raw_ostream &OS);
  Error writeSectionData(raw_ostream &OS);
  void writeRelocations(raw_ostream &OS);
  void writeLinkEditData(raw_ostream &OS);

  void writeBindOpcodes(raw_ostream &OS,
                        std::vector<MachOYAML::BindOpcode> &BindOpcodes);
  // LinkEdit writers
  void writeRebaseOpcodes(raw_ostream &OS);
  void writeBasicBindOpcodes(raw_ostream &OS);
  void writeWeakBindOpcodes(raw_ostream &OS);
  void writeLazyBindOpcodes(raw_ostream &OS);
  void writeNameList(raw_ostream &OS);
  void writeStringTable(raw_ostream &OS);
  void writeExportTrie(raw_ostream &OS);

  void dumpExportEntry(raw_ostream &OS, MachOYAML::ExportEntry &Entry);
  void ZeroToOffset(raw_ostream &OS, size_t offset);

  MachOYAML::Object &Obj;
  bool is64Bit;
  uint64_t fileStart;
  MachO::mach_header_64 Header;

  // Old PPC Object Files didn't have __LINKEDIT segments, the data was just
  // stuck at the end of the file.
  bool FoundLinkEditSeg = false;
};

Error MachOWriter::writeMachO(raw_ostream &OS) {
  fileStart = OS.tell();
  writeHeader(OS);
  writeLoadCommands(OS);
  if (Error Err = writeSectionData(OS))
    return Err;
  writeRelocations(OS);
  if (!FoundLinkEditSeg)
    writeLinkEditData(OS);
  return Error::success();
}

void MachOWriter::writeHeader(raw_ostream &OS) {
  Header.magic = Obj.Header.magic;
  Header.cputype = Obj.Header.cputype;
  Header.cpusubtype = Obj.Header.cpusubtype;
  Header.filetype = Obj.Header.filetype;
  Header.ncmds = Obj.Header.ncmds;
  Header.sizeofcmds = Obj.Header.sizeofcmds;
  Header.flags = Obj.Header.flags;
  Header.reserved = Obj.Header.reserved;

  if (Obj.IsLittleEndian != sys::IsLittleEndianHost)
    MachO::swapStruct(Header);

  auto header_size =
      is64Bit ? sizeof(MachO::mach_header_64) : sizeof(MachO::mach_header);
  OS.write((const char *)&Header, header_size);
}

template <typename SectionType>
SectionType constructSection(MachOYAML::Section Sec) {
  SectionType TempSec;
  memcpy(reinterpret_cast<void *>(&TempSec.sectname[0]), &Sec.sectname[0], 16);
  memcpy(reinterpret_cast<void *>(&TempSec.segname[0]), &Sec.segname[0], 16);
  TempSec.addr = Sec.addr;
  TempSec.size = Sec.size;
  TempSec.offset = Sec.offset;
  TempSec.align = Sec.align;
  TempSec.reloff = Sec.reloff;
  TempSec.nreloc = Sec.nreloc;
  TempSec.flags = Sec.flags;
  TempSec.reserved1 = Sec.reserved1;
  TempSec.reserved2 = Sec.reserved2;
  return TempSec;
}

template <typename StructType>
size_t writeLoadCommandData(MachOYAML::LoadCommand &LC, raw_ostream &OS,
                            bool IsLittleEndian) {
  return 0;
}

template <>
size_t writeLoadCommandData<MachO::segment_command>(MachOYAML::LoadCommand &LC,
                                                    raw_ostream &OS,
                                                    bool IsLittleEndian) {
  size_t BytesWritten = 0;
  for (const auto &Sec : LC.Sections) {
    auto TempSec = constructSection<MachO::section>(Sec);
    if (IsLittleEndian != sys::IsLittleEndianHost)
      MachO::swapStruct(TempSec);
    OS.write(reinterpret_cast<const char *>(&(TempSec)),
             sizeof(MachO::section));
    BytesWritten += sizeof(MachO::section);
  }
  return BytesWritten;
}

template <>
size_t writeLoadCommandData<MachO::segment_command_64>(
    MachOYAML::LoadCommand &LC, raw_ostream &OS, bool IsLittleEndian) {
  size_t BytesWritten = 0;
  for (const auto &Sec : LC.Sections) {
    auto TempSec = constructSection<MachO::section_64>(Sec);
    TempSec.reserved3 = Sec.reserved3;
    if (IsLittleEndian != sys::IsLittleEndianHost)
      MachO::swapStruct(TempSec);
    OS.write(reinterpret_cast<const char *>(&(TempSec)),
             sizeof(MachO::section_64));
    BytesWritten += sizeof(MachO::section_64);
  }
  return BytesWritten;
}

size_t writePayloadString(MachOYAML::LoadCommand &LC, raw_ostream &OS) {
  size_t BytesWritten = 0;
  if (!LC.PayloadString.empty()) {
    OS.write(LC.PayloadString.c_str(), LC.PayloadString.length());
    BytesWritten = LC.PayloadString.length();
  }
  return BytesWritten;
}

template <>
size_t writeLoadCommandData<MachO::dylib_command>(MachOYAML::LoadCommand &LC,
                                                  raw_ostream &OS,
                                                  bool IsLittleEndian) {
  return writePayloadString(LC, OS);
}

template <>
size_t writeLoadCommandData<MachO::dylinker_command>(MachOYAML::LoadCommand &LC,
                                                     raw_ostream &OS,
                                                     bool IsLittleEndian) {
  return writePayloadString(LC, OS);
}

template <>
size_t writeLoadCommandData<MachO::rpath_command>(MachOYAML::LoadCommand &LC,
                                                  raw_ostream &OS,
                                                  bool IsLittleEndian) {
  return writePayloadString(LC, OS);
}

template <>
size_t writeLoadCommandData<MachO::build_version_command>(
    MachOYAML::LoadCommand &LC, raw_ostream &OS, bool IsLittleEndian) {
  size_t BytesWritten = 0;
  for (const auto &T : LC.Tools) {
    struct MachO::build_tool_version tool = T;
    if (IsLittleEndian != sys::IsLittleEndianHost)
      MachO::swapStruct(tool);
    OS.write(reinterpret_cast<const char *>(&tool),
             sizeof(MachO::build_tool_version));
    BytesWritten += sizeof(MachO::build_tool_version);
  }
  return BytesWritten;
}

void ZeroFillBytes(raw_ostream &OS, size_t Size) {
  std::vector<uint8_t> FillData;
  FillData.insert(FillData.begin(), Size, 0);
  OS.write(reinterpret_cast<char *>(FillData.data()), Size);
}

void Fill(raw_ostream &OS, size_t Size, uint32_t Data) {
  std::vector<uint32_t> FillData;
  FillData.insert(FillData.begin(), (Size / 4) + 1, Data);
  OS.write(reinterpret_cast<char *>(FillData.data()), Size);
}

void MachOWriter::ZeroToOffset(raw_ostream &OS, size_t Offset) {
  auto currOffset = OS.tell() - fileStart;
  if (currOffset < Offset)
    ZeroFillBytes(OS, Offset - currOffset);
}

void MachOWriter::writeLoadCommands(raw_ostream &OS) {
  for (auto &LC : Obj.LoadCommands) {
    size_t BytesWritten = 0;
    llvm::MachO::macho_load_command Data = LC.Data;

#define HANDLE_LOAD_COMMAND(LCName, LCValue, LCStruct)                         \
  case MachO::LCName:                                                          \
    if (Obj.IsLittleEndian != sys::IsLittleEndianHost)                         \
      MachO::swapStruct(Data.LCStruct##_data);                                 \
    OS.write(reinterpret_cast<const char *>(&(Data.LCStruct##_data)),          \
             sizeof(MachO::LCStruct));                                         \
    BytesWritten = sizeof(MachO::LCStruct);                                    \
    BytesWritten +=                                                            \
        writeLoadCommandData<MachO::LCStruct>(LC, OS, Obj.IsLittleEndian);     \
    break;

    switch (LC.Data.load_command_data.cmd) {
    default:
      if (Obj.IsLittleEndian != sys::IsLittleEndianHost)
        MachO::swapStruct(Data.load_command_data);
      OS.write(reinterpret_cast<const char *>(&(Data.load_command_data)),
               sizeof(MachO::load_command));
      BytesWritten = sizeof(MachO::load_command);
      BytesWritten +=
          writeLoadCommandData<MachO::load_command>(LC, OS, Obj.IsLittleEndian);
      break;
#include "llvm/BinaryFormat/MachO.def"
    }

    if (LC.PayloadBytes.size() > 0) {
      OS.write(reinterpret_cast<const char *>(LC.PayloadBytes.data()),
               LC.PayloadBytes.size());
      BytesWritten += LC.PayloadBytes.size();
    }

    if (LC.ZeroPadBytes > 0) {
      ZeroFillBytes(OS, LC.ZeroPadBytes);
      BytesWritten += LC.ZeroPadBytes;
    }

    // Fill remaining bytes with 0. This will only get hit in partially
    // specified test cases.
    auto BytesRemaining = LC.Data.load_command_data.cmdsize - BytesWritten;
    if (BytesRemaining > 0) {
      ZeroFillBytes(OS, BytesRemaining);
    }
  }
}

Error MachOWriter::writeSectionData(raw_ostream &OS) {
  for (auto &LC : Obj.LoadCommands) {
    switch (LC.Data.load_command_data.cmd) {
    case MachO::LC_SEGMENT:
    case MachO::LC_SEGMENT_64:
      uint64_t segOff = is64Bit ? LC.Data.segment_command_64_data.fileoff
                                : LC.Data.segment_command_data.fileoff;
      if (0 ==
          strncmp(&LC.Data.segment_command_data.segname[0], "__LINKEDIT", 16)) {
        FoundLinkEditSeg = true;
        writeLinkEditData(OS);
      }
      for (auto &Sec : LC.Sections) {
        ZeroToOffset(OS, Sec.offset);
        // Zero Fill any data between the end of the last thing we wrote and the
        // start of this section.
        if (OS.tell() - fileStart > Sec.offset && Sec.offset != (uint32_t)0)
          return createStringError(
              errc::invalid_argument,
              "wrote too much data somewhere, section offsets don't line up");
        if (0 == strncmp(&Sec.segname[0], "__DWARF", 16)) {
          Error Err = Error::success();
          cantFail(std::move(Err));

          if (0 == strncmp(&Sec.sectname[0], "__debug_str", 16))
            Err = DWARFYAML::emitDebugStr(OS, Obj.DWARF);
          else if (0 == strncmp(&Sec.sectname[0], "__debug_abbrev", 16))
            Err = DWARFYAML::emitDebugAbbrev(OS, Obj.DWARF);
          else if (0 == strncmp(&Sec.sectname[0], "__debug_aranges", 16))
            Err = DWARFYAML::emitDebugAranges(OS, Obj.DWARF);
          else if (0 == strncmp(&Sec.sectname[0], "__debug_ranges", 16))
            Err = DWARFYAML::emitDebugRanges(OS, Obj.DWARF);
          else if (0 == strncmp(&Sec.sectname[0], "__debug_pubnames", 16)) {
            if (Obj.DWARF.PubNames)
              Err = DWARFYAML::emitPubSection(OS, *Obj.DWARF.PubNames,
                                              Obj.IsLittleEndian);
          } else if (0 == strncmp(&Sec.sectname[0], "__debug_pubtypes", 16)) {
            if (Obj.DWARF.PubTypes)
              Err = DWARFYAML::emitPubSection(OS, *Obj.DWARF.PubTypes,
                                              Obj.IsLittleEndian);
          } else if (0 == strncmp(&Sec.sectname[0], "__debug_info", 16))
            Err = DWARFYAML::emitDebugInfo(OS, Obj.DWARF);
          else if (0 == strncmp(&Sec.sectname[0], "__debug_line", 16))
            Err = DWARFYAML::emitDebugLine(OS, Obj.DWARF);

          if (Err)
            return Err;

          continue;
        }

        // Skip if it's a virtual section.
        if (MachO::isVirtualSection(Sec.flags & MachO::SECTION_TYPE))
          continue;

        if (Sec.content) {
          yaml::BinaryRef Content = *Sec.content;
          Content.writeAsBinary(OS);
          ZeroFillBytes(OS, Sec.size - Content.binary_size());
        } else {
          // Fill section data with 0xDEADBEEF.
          Fill(OS, Sec.size, 0xDEADBEEFu);
        }
      }
      uint64_t segSize = is64Bit ? LC.Data.segment_command_64_data.filesize
                                 : LC.Data.segment_command_data.filesize;
      ZeroToOffset(OS, segOff + segSize);
      break;
    }
  }

  return Error::success();
}

// The implementation of makeRelocationInfo and makeScatteredRelocationInfo is
// consistent with how libObject parses MachO binary files. For the reference
// see getStruct, getRelocation, getPlainRelocationPCRel,
// getPlainRelocationLength and related methods in MachOObjectFile.cpp
static MachO::any_relocation_info
makeRelocationInfo(const MachOYAML::Relocation &R, bool IsLE) {
  assert(!R.is_scattered && "non-scattered relocation expected");
  MachO::any_relocation_info MRE;
  MRE.r_word0 = R.address;
  if (IsLE)
    MRE.r_word1 = ((unsigned)R.symbolnum << 0) | ((unsigned)R.is_pcrel << 24) |
                  ((unsigned)R.length << 25) | ((unsigned)R.is_extern << 27) |
                  ((unsigned)R.type << 28);
  else
    MRE.r_word1 = ((unsigned)R.symbolnum << 8) | ((unsigned)R.is_pcrel << 7) |
                  ((unsigned)R.length << 5) | ((unsigned)R.is_extern << 4) |
                  ((unsigned)R.type << 0);
  return MRE;
}

static MachO::any_relocation_info
makeScatteredRelocationInfo(const MachOYAML::Relocation &R) {
  assert(R.is_scattered && "scattered relocation expected");
  MachO::any_relocation_info MRE;
  MRE.r_word0 = (((unsigned)R.address << 0) | ((unsigned)R.type << 24) |
                 ((unsigned)R.length << 28) | ((unsigned)R.is_pcrel << 30) |
                 MachO::R_SCATTERED);
  MRE.r_word1 = R.value;
  return MRE;
}

void MachOWriter::writeRelocations(raw_ostream &OS) {
  for (const MachOYAML::LoadCommand &LC : Obj.LoadCommands) {
    switch (LC.Data.load_command_data.cmd) {
    case MachO::LC_SEGMENT:
    case MachO::LC_SEGMENT_64:
      for (const MachOYAML::Section &Sec : LC.Sections) {
        if (Sec.relocations.empty())
          continue;
        ZeroToOffset(OS, Sec.reloff);
        for (const MachOYAML::Relocation &R : Sec.relocations) {
          MachO::any_relocation_info MRE =
              R.is_scattered ? makeScatteredRelocationInfo(R)
                             : makeRelocationInfo(R, Obj.IsLittleEndian);
          if (Obj.IsLittleEndian != sys::IsLittleEndianHost)
            MachO::swapStruct(MRE);
          OS.write(reinterpret_cast<const char *>(&MRE),
                   sizeof(MachO::any_relocation_info));
        }
      }
    }
  }
}

void MachOWriter::writeBindOpcodes(
    raw_ostream &OS, std::vector<MachOYAML::BindOpcode> &BindOpcodes) {

  for (auto Opcode : BindOpcodes) {
    uint8_t OpByte = Opcode.Opcode | Opcode.Imm;
    OS.write(reinterpret_cast<char *>(&OpByte), 1);
    for (auto Data : Opcode.ULEBExtraData) {
      encodeULEB128(Data, OS);
    }
    for (auto Data : Opcode.SLEBExtraData) {
      encodeSLEB128(Data, OS);
    }
    if (!Opcode.Symbol.empty()) {
      OS.write(Opcode.Symbol.data(), Opcode.Symbol.size());
      OS.write('\0');
    }
  }
}

void MachOWriter::dumpExportEntry(raw_ostream &OS,
                                  MachOYAML::ExportEntry &Entry) {
  encodeSLEB128(Entry.TerminalSize, OS);
  if (Entry.TerminalSize > 0) {
    encodeSLEB128(Entry.Flags, OS);
    if (Entry.Flags & MachO::EXPORT_SYMBOL_FLAGS_REEXPORT) {
      encodeSLEB128(Entry.Other, OS);
      OS << Entry.ImportName;
      OS.write('\0');
    } else {
      encodeSLEB128(Entry.Address, OS);
      if (Entry.Flags & MachO::EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER)
        encodeSLEB128(Entry.Other, OS);
    }
  }
  OS.write(static_cast<uint8_t>(Entry.Children.size()));
  for (auto EE : Entry.Children) {
    OS << EE.Name;
    OS.write('\0');
    encodeSLEB128(EE.NodeOffset, OS);
  }
  for (auto EE : Entry.Children)
    dumpExportEntry(OS, EE);
}

void MachOWriter::writeExportTrie(raw_ostream &OS) {
  dumpExportEntry(OS, Obj.LinkEdit.ExportTrie);
}

template <typename NListType>
void writeNListEntry(MachOYAML::NListEntry &NLE, raw_ostream &OS,
                     bool IsLittleEndian) {
  NListType ListEntry;
  ListEntry.n_strx = NLE.n_strx;
  ListEntry.n_type = NLE.n_type;
  ListEntry.n_sect = NLE.n_sect;
  ListEntry.n_desc = NLE.n_desc;
  ListEntry.n_value = NLE.n_value;

  if (IsLittleEndian != sys::IsLittleEndianHost)
    MachO::swapStruct(ListEntry);
  OS.write(reinterpret_cast<const char *>(&ListEntry), sizeof(NListType));
}

void MachOWriter::writeLinkEditData(raw_ostream &OS) {
  typedef void (MachOWriter::*writeHandler)(raw_ostream &);
  typedef std::pair<uint64_t, writeHandler> writeOperation;
  std::vector<writeOperation> WriteQueue;

  MachO::dyld_info_command *DyldInfoOnlyCmd = 0;
  MachO::symtab_command *SymtabCmd = 0;
  for (auto &LC : Obj.LoadCommands) {
    switch (LC.Data.load_command_data.cmd) {
    case MachO::LC_SYMTAB:
      SymtabCmd = &LC.Data.symtab_command_data;
      WriteQueue.push_back(
          std::make_pair(SymtabCmd->symoff, &MachOWriter::writeNameList));
      WriteQueue.push_back(
          std::make_pair(SymtabCmd->stroff, &MachOWriter::writeStringTable));
      break;
    case MachO::LC_DYLD_INFO_ONLY:
      DyldInfoOnlyCmd = &LC.Data.dyld_info_command_data;
      WriteQueue.push_back(std::make_pair(DyldInfoOnlyCmd->rebase_off,
                                          &MachOWriter::writeRebaseOpcodes));
      WriteQueue.push_back(std::make_pair(DyldInfoOnlyCmd->bind_off,
                                          &MachOWriter::writeBasicBindOpcodes));
      WriteQueue.push_back(std::make_pair(DyldInfoOnlyCmd->weak_bind_off,
                                          &MachOWriter::writeWeakBindOpcodes));
      WriteQueue.push_back(std::make_pair(DyldInfoOnlyCmd->lazy_bind_off,
                                          &MachOWriter::writeLazyBindOpcodes));
      WriteQueue.push_back(std::make_pair(DyldInfoOnlyCmd->export_off,
                                          &MachOWriter::writeExportTrie));
      break;
    }
  }

  llvm::sort(WriteQueue, [](const writeOperation &a, const writeOperation &b) {
    return a.first < b.first;
  });

  for (auto writeOp : WriteQueue) {
    ZeroToOffset(OS, writeOp.first);
    (this->*writeOp.second)(OS);
  }
}

void MachOWriter::writeRebaseOpcodes(raw_ostream &OS) {
  MachOYAML::LinkEditData &LinkEdit = Obj.LinkEdit;

  for (auto Opcode : LinkEdit.RebaseOpcodes) {
    uint8_t OpByte = Opcode.Opcode | Opcode.Imm;
    OS.write(reinterpret_cast<char *>(&OpByte), 1);
    for (auto Data : Opcode.ExtraData)
      encodeULEB128(Data, OS);
  }
}

void MachOWriter::writeBasicBindOpcodes(raw_ostream &OS) {
  writeBindOpcodes(OS, Obj.LinkEdit.BindOpcodes);
}

void MachOWriter::writeWeakBindOpcodes(raw_ostream &OS) {
  writeBindOpcodes(OS, Obj.LinkEdit.WeakBindOpcodes);
}

void MachOWriter::writeLazyBindOpcodes(raw_ostream &OS) {
  writeBindOpcodes(OS, Obj.LinkEdit.LazyBindOpcodes);
}

void MachOWriter::writeNameList(raw_ostream &OS) {
  for (auto NLE : Obj.LinkEdit.NameList) {
    if (is64Bit)
      writeNListEntry<MachO::nlist_64>(NLE, OS, Obj.IsLittleEndian);
    else
      writeNListEntry<MachO::nlist>(NLE, OS, Obj.IsLittleEndian);
  }
}

void MachOWriter::writeStringTable(raw_ostream &OS) {
  for (auto Str : Obj.LinkEdit.StringTable) {
    OS.write(Str.data(), Str.size());
    OS.write('\0');
  }
}

class UniversalWriter {
public:
  UniversalWriter(yaml::YamlObjectFile &ObjectFile)
      : ObjectFile(ObjectFile), fileStart(0) {}

  Error writeMachO(raw_ostream &OS);

private:
  void writeFatHeader(raw_ostream &OS);
  void writeFatArchs(raw_ostream &OS);

  void ZeroToOffset(raw_ostream &OS, size_t offset);

  yaml::YamlObjectFile &ObjectFile;
  uint64_t fileStart;
};

Error UniversalWriter::writeMachO(raw_ostream &OS) {
  fileStart = OS.tell();
  if (ObjectFile.MachO) {
    MachOWriter Writer(*ObjectFile.MachO);
    return Writer.writeMachO(OS);
  }

  writeFatHeader(OS);
  writeFatArchs(OS);

  auto &FatFile = *ObjectFile.FatMachO;
  if (FatFile.FatArchs.size() < FatFile.Slices.size())
    return createStringError(
        errc::invalid_argument,
        "cannot write 'Slices' if not described in 'FatArches'");

  for (size_t i = 0; i < FatFile.Slices.size(); i++) {
    ZeroToOffset(OS, FatFile.FatArchs[i].offset);
    MachOWriter Writer(FatFile.Slices[i]);
    if (Error Err = Writer.writeMachO(OS))
      return Err;

    auto SliceEnd = FatFile.FatArchs[i].offset + FatFile.FatArchs[i].size;
    ZeroToOffset(OS, SliceEnd);
  }

  return Error::success();
}

void UniversalWriter::writeFatHeader(raw_ostream &OS) {
  auto &FatFile = *ObjectFile.FatMachO;
  MachO::fat_header header;
  header.magic = FatFile.Header.magic;
  header.nfat_arch = FatFile.Header.nfat_arch;
  if (sys::IsLittleEndianHost)
    swapStruct(header);
  OS.write(reinterpret_cast<const char *>(&header), sizeof(MachO::fat_header));
}

template <typename FatArchType>
FatArchType constructFatArch(MachOYAML::FatArch &Arch) {
  FatArchType FatArch;
  FatArch.cputype = Arch.cputype;
  FatArch.cpusubtype = Arch.cpusubtype;
  FatArch.offset = Arch.offset;
  FatArch.size = Arch.size;
  FatArch.align = Arch.align;
  return FatArch;
}

template <typename StructType>
void writeFatArch(MachOYAML::FatArch &LC, raw_ostream &OS) {}

template <>
void writeFatArch<MachO::fat_arch>(MachOYAML::FatArch &Arch, raw_ostream &OS) {
  auto FatArch = constructFatArch<MachO::fat_arch>(Arch);
  if (sys::IsLittleEndianHost)
    swapStruct(FatArch);
  OS.write(reinterpret_cast<const char *>(&FatArch), sizeof(MachO::fat_arch));
}

template <>
void writeFatArch<MachO::fat_arch_64>(MachOYAML::FatArch &Arch,
                                      raw_ostream &OS) {
  auto FatArch = constructFatArch<MachO::fat_arch_64>(Arch);
  FatArch.reserved = Arch.reserved;
  if (sys::IsLittleEndianHost)
    swapStruct(FatArch);
  OS.write(reinterpret_cast<const char *>(&FatArch),
           sizeof(MachO::fat_arch_64));
}

void UniversalWriter::writeFatArchs(raw_ostream &OS) {
  auto &FatFile = *ObjectFile.FatMachO;
  bool is64Bit = FatFile.Header.magic == MachO::FAT_MAGIC_64;
  for (auto Arch : FatFile.FatArchs) {
    if (is64Bit)
      writeFatArch<MachO::fat_arch_64>(Arch, OS);
    else
      writeFatArch<MachO::fat_arch>(Arch, OS);
  }
}

void UniversalWriter::ZeroToOffset(raw_ostream &OS, size_t Offset) {
  auto currOffset = OS.tell() - fileStart;
  if (currOffset < Offset)
    ZeroFillBytes(OS, Offset - currOffset);
}

} // end anonymous namespace

namespace llvm {
namespace yaml {

bool yaml2macho(YamlObjectFile &Doc, raw_ostream &Out, ErrorHandler EH) {
  UniversalWriter Writer(Doc);
  if (Error Err = Writer.writeMachO(Out)) {
    handleAllErrors(std::move(Err),
                    [&](const ErrorInfoBase &Err) { EH(Err.message()); });
    return false;
  }
  return true;
}

} // namespace yaml
} // namespace llvm
