#include "../src/runtime/Runtime2026.h"
#include "../src/model/PatchImage.h"
#include "../src/model/PersonSnapshot.h"
#include "../src/runtime/GameText.h"
#include "../src/runtime/AppPaths.h"
#include "../src/app/application/Controller.h"
#include "../src/app/ui/RuntimeStatusText.h"
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <crtdbg.h>
#include <fstream>
#include <sstream>
#include <memory>
void presentationSpeedMachineCodeTest();

// Explicit friend seam for recovered-layout regressions, unavailable to GUI clients.
struct RuntimeTestAccess2026 {
    Runtime2026& runtime;
    bool rebase() { return runtime.rebase(); }
    unsigned appliedPresentationSpeed() const { return runtime.appliedSpeed_; }
    template<class T> T& field(size_t offset) { return runtime.field<T>(offset); }
    void copyIn(size_t offset, const void* source, size_t size) { runtime.copyIn(offset, source, size); }
    void copyOut(size_t offset, void* destination, size_t size) { runtime.copyOut(offset, destination, size); }
    void dataIn(DWORD address, const void* source, size_t size) { runtime.dataIn(address, source, size); }
    void dataOut(DWORD address, void* destination, size_t size) { runtime.dataOut(address, destination, size); }
    bool operation(const std::function<void()>& action) { return runtime.runOperation(action); }
    void write(DWORD target, const void* bytes, size_t size) { runtime.writePatch(target, bytes, size); }
    void read(DWORD source, void* bytes, size_t size) { runtime.readMemory(runtime.session().process,source,bytes,size,nullptr); }
    const BYTE* source(DWORD address, size_t size) { return runtime.sourceVA(address,size); }
    const BYTE* nativeSource(std::uintptr_t address, size_t size) { return runtime.nativeDataBytes(address,size); }
    BYTE* stateBytes(size_t offset, size_t size) { return runtime.stateBytes(offset,size); }
    template<class T> T* pointer(DWORD address) { return runtime.pointer<T>(address); }
    void descriptor(DWORD address, PatchSource2026 source) { runtime.writeDescriptor(address,source); }
    void descriptorRange(DWORD address, size_t count, PatchSource2026 source) { runtime.writeDescriptorRange(address,count,source); }
    void inlineRange(DWORD address, size_t count) { runtime.writeInlineRange(address,count); }
    void capture(std::uintptr_t source, std::uintptr_t destination, size_t size) { runtime.captureGameBytes(source,destination,size); }
};
namespace app {
struct ControllerTestAccess {
    static Runtime2026& runtime(app::Controller& controller) { return controller.runtime_; }
};
}
namespace {
std::unordered_map<DWORD,BYTE> memory;
std::unordered_map<DWORD,BYTE> singleByteWrites;
size_t writes=0, reads=0, releases=0, closes=0;
std::vector<std::pair<DWORD,SIZE_T>>* recordedReads = nullptr;
// Opt-in capture of written byte blocks, so tests can assert on injected code.
bool recordWrites=false;
std::vector<std::pair<DWORD,std::vector<BYTE>>> recordedWrites;
std::ofstream memoryTrace;
void traceMemory(char operation, DWORD address, const void* bytes, SIZE_T size) {
    if (!memoryTrace.is_open()) return;
    const DWORD length=static_cast<DWORD>(size);
    memoryTrace.write(&operation,1);
    memoryTrace.write(reinterpret_cast<const char*>(&address),sizeof(address));
    memoryTrace.write(reinterpret_cast<const char*>(&length),sizeof(length));
    memoryTrace.write(static_cast<const char*>(bytes),length);
    if (!memoryTrace) throw std::runtime_error("cannot write memory trace");
}
bool present=true;
DWORD base=0x10000000;
std::unordered_map<DWORD,DWORD> protections;
bool allocated=false;
bool speedAllocated=false;
std::unordered_map<DWORD,SIZE_T> allocations;
DWORD nextAuxOffset=0x20000;
DWORD barrierEip=0;
unsigned barrierSuspended=0;
enum class BarrierFault { None, Snapshot, Open, Suspend, Context, Resume };
BarrierFault barrierFault=BarrierFault::None;
bool processAlive=true, identityAvailable=true;
DWORD processPid=123, windowPid=123, processCreated=1, windowHandle=42;
size_t exitAfterRead=0;
std::function<void()> writeCallback;
enum class Fault { None, Open, Read, Write, Flush, Query, Protect, Allocate, Release, Close, Resolve, Message, Count };
std::array<size_t,static_cast<size_t>(Fault::Count)> calls{};
Fault fault=Fault::None;
size_t faultAt=0;
bool fired=false, blockRecovery=false, partialFailure=false, shortSuccess=true;
bool fails(Fault kind) {
    const auto n=++calls[static_cast<size_t>(kind)];
    if (fired && blockRecovery && kind==Fault::Write) { SetLastError(ERROR_WRITE_FAULT); return true; }
    if (!fired && fault==kind && n==faultAt) { fired=true; SetLastError(ERROR_ACCESS_DENIED); return true; }
    return false;
}
SIZE_T WINAPI query(HANDLE,LPCVOID address,PMEMORY_BASIC_INFORMATION region,SIZE_T) {
    if (fails(Fault::Query)) return 0;
    const DWORD page=reinterpret_cast<DWORD>(address)&~0xFFFu;
    region->BaseAddress=reinterpret_cast<LPVOID>(page);
    region->RegionSize=0x1000; region->State=MEM_COMMIT;
    region->Protect=protections.count(page)?protections[page]:PAGE_READONLY;
    return sizeof(*region);
}
void check(bool ok,const char* message) {
    if (!ok) { std::fprintf(stderr,"FAILED: %s\n",message); std::exit(1); }
}
HWND WINAPI findWindow(LPCSTR,LPCSTR) {return present?reinterpret_cast<HWND>(windowHandle):nullptr;}
DWORD WINAPI windowProcess(HWND,LPDWORD pid) {if(fails(Fault::Resolve)) return 0; *pid=windowPid;return 1;}
DWORD WINAPI processId(HANDLE) { return identityAvailable?processPid:0; }
BOOL WINAPI processTimes(HANDLE,LPFILETIME created,LPFILETIME,LPFILETIME,LPFILETIME) {
    if(!identityAvailable) return FALSE;
    *created={processCreated,0};return TRUE;
}
DWORD WINAPI waitProcess(HANDLE,DWORD timeout) {
    check(timeout==0,"liveness check never blocks");
    return processAlive?WAIT_TIMEOUT:WAIT_OBJECT_0;
}
HANDLE WINAPI openProcess(DWORD access,BOOL,DWORD) {
    check((access&PROCESS_QUERY_INFORMATION)!=0,"query access requested for protection regions");
    check((access&SYNCHRONIZE)!=0,"synchronize access requested for exit detection");
    return fails(Fault::Open)?nullptr:reinterpret_cast<HANDLE>(43);
}
BOOL WINAPI closeHandle(HANDLE) {if(fails(Fault::Close)) return FALSE; ++closes;return TRUE;}
LPVOID WINAPI allocate(HANDLE,LPVOID,SIZE_T size,DWORD,DWORD) {
    if(fails(Fault::Allocate)) return nullptr;
    DWORD address = 0;
    if (size==presentation2026::PayloadSize) { speedAllocated=true; address=base+0x10000; }
    else if (size==0xB000) { allocated=true; address=base; }
    else { address=base+nextAuxOffset; nextAuxOffset+=0x10000; }
    allocations[address]=size;
    return reinterpret_cast<LPVOID>(address);
}
BOOL WINAPI release(HANDLE,LPVOID address,SIZE_T size,DWORD type) {
    if(fails(Fault::Release)) return FALSE;
    const DWORD target=reinterpret_cast<DWORD>(address);
    const auto found=allocations.find(target);
    check(found!=allocations.end() && size==0 && type==MEM_RELEASE,"allocation released once");
    const SIZE_T allocatedSize=found->second;
    for(SIZE_T i=0;i<allocatedSize;++i) memory.erase(target+static_cast<DWORD>(i));
    allocations.erase(found);
    if (target==base) { check(allocated,"main allocation was tracked"); allocated=false; }
    else if (target==base+0x10000) { check(speedAllocated,"speed allocation was tracked"); speedAllocated=false; }
    ++releases;
    return TRUE;
}
BOOL WINAPI protect(HANDLE,LPVOID address,SIZE_T size,DWORD value,PDWORD old) {
    if(fails(Fault::Protect)) return FALSE;
    const DWORD page=reinterpret_cast<DWORD>(address);
    *old=protections.count(page)?protections[page]:PAGE_READONLY;
    for(SIZE_T i=0;i<size;i+=0x1000) protections[page+static_cast<DWORD>(i)]=value;
    return TRUE;
}
BOOL WINAPI read(HANDLE,LPCVOID address,LPVOID buffer,SIZE_T size,SIZE_T* count) {
    if(fails(Fault::Read)) {
        *count=partialFailure?size-1:0;
        if (*count) std::memset(buffer,0xE1,*count);
        return partialFailure && shortSuccess;
    }
    ++reads;
    check(size<0x10000 || size==san9::PersonSnapshot::Count*san9::PersonSnapshot::RecordSize,"bounded read");
    if(recordedReads) recordedReads->emplace_back(reinterpret_cast<DWORD>(address),size);
    auto p=static_cast<BYTE*>(buffer);
    for(size_t i=0;i<size;++i) p[i]=memory[reinterpret_cast<DWORD>(address)+static_cast<DWORD>(i)];
    traceMemory('R',reinterpret_cast<DWORD>(address),buffer,size);
    if(exitAfterRead && reads==exitAfterRead) { processAlive=false; allocated=false; }
    *count=size;return TRUE;
}
BOOL WINAPI write(HANDLE,LPVOID address,LPCVOID buffer,SIZE_T size,SIZE_T* count) {
    const bool failure=fails(Fault::Write);
    const DWORD failureError=failure?GetLastError():0;
    const SIZE_T requested=size;
    if(failure) size=partialFailure?size/2:0;
    if(failure && !size) { *count=0;return FALSE; }
    check(size>0 && size<0x10000,"bounded write");
    ++writes;
    const DWORD destination=reinterpret_cast<DWORD>(address);
    const bool inGame = destination>=0xA0000 && destination+size<=0x2000000;
    bool inAllocation = false;
    for (const auto& entry : allocations) {
        if (destination>=entry.first && destination+size<=entry.first+entry.second) { inAllocation=true; break; }
    }
    if (!inGame && !inAllocation) {
        std::fprintf(stderr,"invalid target: target=%p source=%p size=%zu write=%zu base=%08lX\n",address,buffer,size,writes,base);
    }
    check(inGame || inAllocation,"write stays in game or allocation");
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(buffer,&region,sizeof(region)) || region.State!=MEM_COMMIT
        || (region.Protect & (PAGE_NOACCESS|PAGE_GUARD))) {
        std::fprintf(stderr,"invalid source: target=%p source=%p size=%zu write=%zu\n",address,buffer,size,writes);
        std::exit(1);
    }
    const auto p=static_cast<const BYTE*>(buffer);
    traceMemory('W',destination,buffer,size);
    if (size==1) singleByteWrites[destination]=p[0];
    if (recordWrites) recordedWrites.emplace_back(destination,std::vector<BYTE>(p,p+size));
    for(size_t i=0;i<size;++i) memory[reinterpret_cast<DWORD>(address)+static_cast<DWORD>(i)]=p[i];
    *count=size;
    if (writeCallback) { auto callback=std::move(writeCallback); writeCallback={}; callback(); }
    if (failure) SetLastError(failureError);
    return failure?(partialFailure && shortSuccess):size==requested;
}
BOOL WINAPI flush(HANDLE,LPCVOID,SIZE_T) {return !fails(Fault::Flush);}
UINT WINAPI registerMessage(LPCSTR) {return fails(Fault::Message)?0:0xC001;}
HANDLE WINAPI snapshot(DWORD,DWORD) {
    if (barrierFault==BarrierFault::Snapshot) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    return reinterpret_cast<HANDLE>(77);
}
BOOL WINAPI threadFirst(HANDLE,LPTHREADENTRY32 entry) { entry->th32OwnerProcessID=processPid; entry->th32ThreadID=321; return TRUE; }
BOOL WINAPI threadNext(HANDLE,LPTHREADENTRY32) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
HANDLE WINAPI openThread(DWORD,BOOL,DWORD) {
    if (barrierFault==BarrierFault::Open) { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    return reinterpret_cast<HANDLE>(78);
}
DWORD WINAPI suspendThread(HANDLE) {
    if (barrierFault==BarrierFault::Suspend) { SetLastError(ERROR_ACCESS_DENIED); return MAXDWORD; }
    return barrierSuspended++;
}
DWORD WINAPI resumeThread(HANDLE) {
    if (barrierFault==BarrierFault::Resume) { SetLastError(ERROR_ACCESS_DENIED); return MAXDWORD; }
    check(barrierSuspended>0,"barrier owns one suspension count"); return barrierSuspended--;
}
BOOL WINAPI threadContext(HANDLE,LPCONTEXT context) {
    if (barrierFault==BarrierFault::Context) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    context->Eip=barrierEip; return TRUE;
}
BOOL WINAPI moduleFirst(HANDLE,LPMODULEENTRY32W entry) {
    entry->modBaseAddr=reinterpret_cast<BYTE*>(0x70000000); entry->modBaseSize=0x100000;
    wcscpy_s(entry->szModule,L"winmm.dll"); return TRUE;
}
BOOL WINAPI moduleNext(HANDLE,LPMODULEENTRY32W) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
ProcessApi2026 api() {
    return {findWindow,windowProcess,openProcess,closeHandle,allocate,release,protect,read,write,flush,registerMessage,query,
        processId,processTimes,waitProcess,snapshot,threadFirst,threadNext,openThread,suspendThread,resumeThread,
        threadContext,moduleFirst,moduleNext};
}
void store(DWORD address,DWORD value) {
    for(unsigned i=0;i<4;++i) memory[address+i]=static_cast<BYTE>(value>>(8*i));
}
DWORD load(DWORD address,unsigned size=4) {
    DWORD value=0;for(unsigned i=0;i<size;++i) value|=DWORD(memory[address+i])<<(8*i);return value;
}
void version(Runtime2026& runtime) {
    const DWORD address=RuntimeTestAccess2026{runtime}.field<DWORD>(6444)+RuntimeTestAccess2026{runtime}.field<DWORD>(6448);
    store(address,0xFEEF04BD);store(address+8,0x10000);store(address+12,0x10000);
}
void rebaseTest() {
    Runtime2026 runtime(api());
    san9::PatchImage model;
    for(DWORD value:{0x10000000u,0x22000000u,0xA0000u}) {
        RuntimeTestAccess2026{runtime}.field<DWORD>(96)=value;
        check(RuntimeTestAccess2026{runtime}.rebase(),"rebase changes base");
        const auto changes=model.rebase(value);
        for(const auto& change:changes) {
            DWORD got;
            if(change.slot.objectField) got=RuntimeTestAccess2026{runtime}.field<DWORD>(change.slot.location);
            else RuntimeTestAccess2026{runtime}.dataOut(change.slot.location,&got,4);
            check(got==change.after,"all 969 runtime relocation slots match independent model");
        }
        RuntimeTestAccess2026{runtime}.dataIn(0x420050,&value,4);
    }
}
void branchTest(bool options, bool alternateTarget=false) {
    base=options?0x22000000:0x10000000;
    memory.clear(); singleByteWrites.clear(); writes=reads=releases=closes=0;
    Runtime2026 runtime(api());
    version(runtime);
    // Deliberately vary the descriptor value: the target must be its scalar
    // DWORD + 3, never the address of Runtime2026's local array view + 3 ints.
    DWORD descriptorTarget=0;
    RuntimeTestAccess2026{runtime}.dataOut(0x41FD34,&descriptorTarget,sizeof(descriptorTarget));
    check(descriptorTarget==0x508192,"descriptor value matches original sample");
    if (alternateTarget) {
        descriptorTarget=options?0x01800200:0x01800100;
        RuntimeTestAccess2026{runtime}.dataIn(0x41FD34,&descriptorTarget,sizeof(descriptorTarget));
    }
    san9::ParameterConfig config;
    if(options) for(const auto& f:san9::ParameterConfig::fields()) if(f.mask) config.set(f.controlId,1);
    runtime.setParameters(config);
    if(options) {
        RuntimeTestAccess2026{runtime}.field<DWORD>(4412)=1; RuntimeTestAccess2026{runtime}.field<DWORD>(4416)=1;
        RuntimeTestAccess2026{runtime}.field<BYTE>(san9::layout2026::SkillFlags)=3; RuntimeTestAccess2026{runtime}.field<BYTE>(san9::layout2026::FormationFlags)=3;
    }
    for(int i=0;i<6;++i) {
        runtime.modify(ModifyReason2026::TimerTick);
    }
    check(RuntimeTestAccess2026{runtime}.field<DWORD>(140)==0,"initial injection branches report no errors");
    check(writes>900,"full injection path executed in fake memory");
    check(RuntimeTestAccess2026{runtime}.field<DWORD>(96)==base,"dynamic allocation used");
    PatchDescriptor2026 descriptors[2]{};
    RuntimeTestAccess2026{runtime}.dataOut(0x42F648,descriptors,sizeof(descriptors));
    check(descriptors[0].target==0x508024 && descriptors[0].length==12,
        "first hook descriptor retains target and width");
    check(descriptors[1].target==descriptorTarget && descriptors[1].length==13,
        "second hook descriptor uses scalar array value and width");
    check(singleByteWrites.count(descriptorTarget+3)==1,"scalar descriptor target received a one-byte write");
    check(singleByteWrites.at(descriptorTarget+3)==(options?3u:1u),"scalar descriptor single-byte write value");
    check(load(descriptorTarget+3,1)==(options?3u:1u),"scalar descriptor target uses byte offset +3");
    if(options) {
        check(RuntimeTestAccess2026{runtime}.field<DWORD>(4416)==0,"city configuration applied");
        check((RuntimeTestAccess2026{runtime}.field<BYTE>(san9::layout2026::SkillFlags)&2)==0,"skill configuration applied");
        check((RuntimeTestAccess2026{runtime}.field<BYTE>(san9::layout2026::FormationFlags)&2)==0,"formation configuration applied");
    }
    check(load(4733494,2)==(options?59792u:33807u),"2026 disable NPC merge option");
    DWORD target=0;RuntimeTestAccess2026{runtime}.dataOut(0x42B5B0,&target,4);
    check(load(target+8,2)==(options?30089u:59011u),"2026 unlimited NPC troop option");
    config.set(1164,options?0:1); config.set(1165,options?0:1);
    runtime.setParameters(config);runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(load(4733494,2)==(options?33807u:59792u),"live configuration branch switches merge option");
    check(load(target+8,2)==(options?59011u:30089u),"live configuration branch switches troop option");
    runtime.modify(ModifyReason2026::GameNotification);runtime.modify(ModifyReason2026::GameDialogClosed);
    check(RuntimeTestAccess2026{runtime}.field<DWORD>(140)==0,"load/new-game/configuration branches report no errors");
    const auto injected=writes;
    runtime.undo();
    check(releases==1 && closes==1,"undo releases allocation and handle");
    check(RuntimeTestAccess2026{runtime}.field<HANDLE>(112)==nullptr,"undo resets process state");
    check(writes-injected>400,"complete recovered undo path executes");
    check(load(4733494,2)==0u,"undo restores actual pre-session bytes");
    check(runtime.session().journal.entries().empty(),"undo clears recovered journal");
    std::printf("options=%d alternateTarget=%d: %zu writes, %zu reads; undo=%zu writes\n",options,alternateTarget,writes,reads,writes-injected);
}
void unsupportedVersionTest() {
    memory.clear();writes=reads=releases=closes=0;
    Runtime2026 runtime(api());
    for(int i=0;i<6;++i) runtime.modify(ModifyReason2026::TimerTick);
    check(writes==0 && closes==1 && releases==0,"unsupported game version receives no writes");
    check(RuntimeTestAccess2026{runtime}.field<HANDLE>(112)==nullptr,"unsupported game handle closed");
}
void resetBackend() {
    writeCallback = {};
    recordWrites=false; recordedWrites.clear();
    memory.clear(); singleByteWrites.clear(); protections.clear();
    writes=reads=releases=closes=0; allocated=false; present=true; base=0x10000000;
    speedAllocated=false; allocations.clear(); nextAuxOffset=0x20000;
    barrierEip=0; barrierSuspended=0; barrierFault=BarrierFault::None;
    calls.fill(0); fault=Fault::None; faultAt=0; fired=false; blockRecovery=false; partialFailure=false; shortSuccess=true;
    processAlive=identityAvailable=true;processPid=windowPid=123;processCreated=1;windowHandle=42;exitAfterRead=0;
}
void initialModify(Runtime2026& runtime) { for(int i=0;i<6;++i) runtime.modify(ModifyReason2026::TimerTick); }
void dispatchTest() {
    resetBackend();
    Runtime2026 runtime(api()); version(runtime);
    for(int i=0;i<5;++i) runtime.modify(ModifyReason2026::TimerTick);
    check(writes==0 && !allocated,"connection delay performs no injection");
    runtime.modify(ModifyReason2026::TimerTick);
    check(allocated && writes>900,"sixth tick installs the complete injection");
    const auto installedWrites=writes, installedReads=reads;
    for(int i=0;i<12;++i) runtime.modify(ModifyReason2026::TimerTick);
    check(writes==installedWrites && reads==installedReads,"connected timer only animates status");
    san9::ParameterConfig config;
    config.set(1164,1); runtime.setParameters(config);
    runtime.modify(ModifyReason2026::GameNotification);
    runtime.modify(ModifyReason2026::GameDialogClosed);
    check(load(4733494,2)==33807u,"game events do not apply unconfirmed parameter patches");
    runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(load(4733494,2)==59792u,"confirmation applies parameter patches");
    runtime.undo();
    base=0x22000000; version(runtime); initialModify(runtime);
    check(RuntimeTestAccess2026{runtime}.field<DWORD>(96)==base,
        "reconnection installs at the new allocation base");
    check(load(4733494,2)==59792u,"reconnection reapplies desired parameters");
    runtime.undo();
    check(releases==2 && closes==2,"both connected sessions clean up");
}
void checkRestored(const std::unordered_map<DWORD,BYTE>& before) {
    for(const auto& pair:memory) {
        const auto found=before.find(pair.first);
        check(pair.second==(found==before.end()?0:found->second),"all original bytes restored");
    }
    for(const auto& pair:before) check(memory[pair.first]==pair.second,"original nonzero bytes preserved");
    for(const auto& pair:protections) check(pair.second==PAGE_READONLY,"original page protection restored");
    check(!allocated,"no allocation remains");
}
void diagnosticsTest() {
    constexpr DWORD address=0x600000;
    const BYTE replacement[8]={8,7,6,5,4,3,2,1};
    for(bool success:{false,true}) {
        resetBackend();
        Runtime2026 runtime(api()); version(runtime); initialModify(runtime);
        store(address,0x12345678); store(address+4,0xABCDEF01);
        const auto baseline=memory;
        fault=Fault::Write; faultAt=calls[static_cast<size_t>(Fault::Write)]+1;
        partialFailure=true; shortSuccess=success; blockRecovery=true;
        auto access=RuntimeTestAccess2026{runtime};
        check(!access.operation([&]{access.write(address,replacement,sizeof(replacement));}),
            "partial write and failed recovery report failure");
        const auto failed=runtime.status();
        check(runtime.session().phase==SessionPhase2026::RecoveryRequired,
            "diagnostic failure retains pending recovery");
        check(failed.failure.operation==RuntimeOperation2026::Write && failed.failure.target==address
            && failed.failure.requested==8 && failed.failure.transferred==4,
            "original write diagnostic includes target and actual transfer");
        check(failed.failure.systemError==(success?0:ERROR_ACCESS_DENIED),
            "short success has no stale system error");
        check(failed.recoveryFailure.operation==RuntimeOperation2026::RestoreBytes
            && failed.recoveryFailure.target==address && failed.recoveryFailure.requested==8
            && failed.recoveryFailure.transferred==4
            && failed.recoveryFailure.systemError==(success?0:ERROR_WRITE_FAULT),
            "recovery failure is recorded independently with its own API error");
        const auto text=app::statusDetails(failed);
        check(text.find(L"0x00600000")!=std::wstring::npos && text.find(L"4/8")!=std::wstring::npos
            && text.find(L"恢复失败")!=std::wstring::npos,
            "presentation exposes address, short transfer and recovery cause");
        check(app::statusText(failed).find(L"0x00600000")==std::wstring::npos,
            "narrow status control keeps a summary while hover details retain diagnostics");
        const auto pending=runtime.session().journal.size();
        runtime.modify(ModifyReason2026::TimerTick);
        check(runtime.session().journal.size()==pending && runtime.status().failure.operation==RuntimeOperation2026::Write
            && runtime.status().recoveryFailure.operation==RuntimeOperation2026::RestoreBytes,
            "blocked modification retains both diagnostics and pending history");
        blockRecovery=false; partialFailure=false;
        SetLastError(ERROR_INVALID_DATA);
        check(runtime.retryRollback(),"diagnostic recovery can be retried");
        const auto restored=runtime.status();
        check(!restored.recoveryFailure && restored.failure.operation==failed.failure.operation
            && restored.failure.target==failed.failure.target && restored.failure.requested==8
            && restored.failure.transferred==4 && restored.failure.systemError==failed.failure.systemError,
            "successful retry clears recovery failure and retains the original cause");
        check(memory==baseline,"retry restores complete bytes after a partial recovery write");
        runtime.modify(ModifyReason2026::TimerTick);
        check(!runtime.status().failure && !runtime.status().recoveryFailure,
            "next operation clears previous diagnostics");
        check(runtime.undo(),"diagnostic session can be undone");
    }
    for(bool success:{false,true}) {
        resetBackend(); Runtime2026 runtime(api()); version(runtime); initialModify(runtime);
        fault=Fault::Read; faultAt=calls[static_cast<size_t>(Fault::Read)]+1;
        partialFailure=true; shortSuccess=success;
        auto access=RuntimeTestAccess2026{runtime};
        check(!access.operation([&]{access.write(address,replacement,sizeof(replacement));}),"preimage read failure rolls back");
        const auto diagnostic=runtime.status().failure;
        check(diagnostic.operation==RuntimeOperation2026::ReadOriginal && diagnostic.target==address
            && diagnostic.requested==8 && diagnostic.transferred==7
            && diagnostic.systemError==(success?0:ERROR_ACCESS_DENIED),
            "preimage read failure preserves transfer details without stale errors");
        check(!runtime.status().recoveryFailure && runtime.undo(),"preimage read failure needs no pending recovery");
    }
    resetBackend(); Runtime2026 runtime(api()); version(runtime); initialModify(runtime);
    fault=Fault::Flush; faultAt=calls[static_cast<size_t>(Fault::Flush)]+1;
    auto access=RuntimeTestAccess2026{runtime};
    check(!access.operation([&]{access.write(address,replacement,sizeof(replacement));}),"cache failure rolls back completed write");
    const auto diagnostic=runtime.status().failure;
    check(diagnostic.operation==RuntimeOperation2026::Flush && diagnostic.target==address
        && diagnostic.requested==8 && diagnostic.transferred==8 && diagnostic.systemError==ERROR_ACCESS_DENIED,
        "cache failure identifies fully transferred write and original API error");
    check(!runtime.status().recoveryFailure,"cache failure recovery succeeds");
    fault=Fault::Read; fired=false; faultAt=calls[static_cast<size_t>(Fault::Read)]+1;
    partialFailure=true; shortSuccess=false;
    BYTE snapshot[8]{};
    check(!access.operation([&]{access.read(address,snapshot,sizeof(snapshot));}),"snapshot read failure is structured");
    check(runtime.status().failure.operation==RuntimeOperation2026::Read
        && runtime.status().failure.target==address && runtime.status().failure.requested==8
        && runtime.status().failure.transferred==7 && runtime.status().failure.systemError==ERROR_ACCESS_DENIED,
        "snapshot read reports its own operation and partial transfer");
    runtime.modify(ModifyReason2026::TimerTick);
    partialFailure=false;
    fault=Fault::Flush; fired=false; faultAt=calls[static_cast<size_t>(Fault::Flush)]+1;
    check(!runtime.undo(),"undo cache failure retains recovery state");
    const auto recovery=runtime.status();
    check(!recovery.failure && recovery.recoveryFailure.operation==RuntimeOperation2026::RestoreFlush
        && recovery.recoveryFailure.requested>0
        && recovery.recoveryFailure.transferred==recovery.recoveryFailure.requested
        && recovery.recoveryFailure.systemError==ERROR_ACCESS_DENIED,
        "undo distinguishes restored bytes from failed cache flush without inventing an original failure");
    check(runtime.retryRollback() && !runtime.status().failure && !runtime.status().recoveryFailure,
        "successful undo retry clears the recovery diagnostic");
    resetBackend();
    std::puts("Structured failure causes, partial transfers, recovery retry and status text passed.");
}
void journalTest() {
    resetBackend();
    constexpr DWORD address=0x600000;
    for(DWORD i=0;i<12;++i) memory[address+i]=static_cast<BYTE>(i+1);
    const auto original=memory;
    std::vector<Change2026> history;
    auto change=[&](DWORD first,SIZE_T size,BYTE value) {
        Change2026 entry{ChangeKind2026::Bytes,reinterpret_cast<HANDLE>(43),reinterpret_cast<LPVOID>(first),size};
        for(SIZE_T i=0;i<size;++i) {
            entry.before.push_back(memory[first+static_cast<DWORD>(i)]);
            memory[first+static_cast<DWORD>(i)]=value;
        }
        entry.completed=true;entry.transferred=size;history.push_back(std::move(entry));
    };
    // Duplicate, contained, left/right overlap, and a write bridging covered islands.
    change(address+2,3,50);change(address+8,2,60);change(address+2,3,70);
    change(address,12,80);change(address+4,4,90);
    auto compact=compactCommittedChanges2026(history);
    size_t bytes=0;for(const auto& entry:compact) bytes+=entry.size;
    check(bytes==12,"overlapping commits retain each original byte once");
    check(compactCommittedChanges2026(compact).size()==compact.size(),"compaction is idempotent");
    Change2026 barrier{ChangeKind2026::Protection,reinterpret_cast<HANDLE>(43),reinterpret_cast<LPVOID>(address),12};
    barrier.completed=true;history.push_back(barrier);
    change(address+3,5,100);
    compact=compactCommittedChanges2026(history);
    check(compact.back().before==std::vector<BYTE>({80,90,90,90,90}),"resource barrier preserves intermediate restore bytes");
    check(compact[compact.size()-2].kind==ChangeKind2026::Protection,"resource ordering is preserved");
    for(auto entry=compact.rbegin();entry!=compact.rend();++entry) {
        if(entry->kind==ChangeKind2026::Bytes)
            for(SIZE_T i=0;i<entry->size;++i) memory[reinterpret_cast<DWORD>(entry->address)+static_cast<DWORD>(i)]=entry->before[i];
    }
    check(memory==original,"compacted overlapping history restores the original image");
    history.back().completed=false;
    bool rejected=false;try { compactCommittedChanges2026(history); } catch(const std::logic_error&) { rejected=true; }
    check(rejected && !history.back().completed,"unfinished history is rejected without mutation");
    history.back().completed=true;
    history.back().process=reinterpret_cast<HANDLE>(44);
    auto secondProcess=history.back();secondProcess.process=reinterpret_cast<HANDLE>(45);
    history.push_back(secondProcess);
    check(compactCommittedChanges2026(history).back().process==secondProcess.process,
        "matching addresses in different processes retain separate restore records");
    history.back().before.pop_back();
    rejected=false;try { compactCommittedChanges2026(history); } catch(const std::logic_error&) { rejected=true; }
    check(rejected,"malformed original byte range is rejected");

    ChangeJournal2026 journal;
    journal.begin();
    journal.append(barrier);
    journal.commit();
    const auto committedBoundary=journal.boundary();
    journal.begin();
    journal.append({ChangeKind2026::Bytes,reinterpret_cast<HANDLE>(43),reinterpret_cast<LPVOID>(address),8});
    rejected=false;try { journal.commit(); } catch(const std::logic_error&) { rejected=true; }
    check(rejected && journal.size()==2 && journal.boundary()==committedBoundary
        && journal.entries().front().completed && !journal.entries().back().completed,
        "failed journal commit preserves the committed boundary and pending records");
    journal.discardLast();
    journal.restoreAll();
    check(journal.boundary()==0 && journal.size()==1,"undo boundary retains owned resources until restored");
    journal.clear();
    check(journal.empty() && journal.boundary()==0,"cleared journal has no pending boundary");

    resetBackend();
    auto owned=std::make_unique<Runtime2026>(api());auto& runtime=*owned;
    version(runtime);store(0x483A36,0x12345678);
    const auto before=memory;
    initialModify(runtime);
    runtime.modify(ModifyReason2026::ParametersConfirmed);
    runtime.modify(ModifyReason2026::GameNotification);
    runtime.modify(ModifyReason2026::GameDialogClosed);
    const auto size=runtime.session().journal.entries().size();
    for(int i=0;i<1000;++i) {
        runtime.modify(ModifyReason2026::TimerTick);
        check(runtime.session().result==OperationResult2026::Committed && runtime.session().journal.entries().size()==size,
            "long-running timer journal stays bounded");
    }
    for(int i=0;i<20;++i) {
        san9::ParameterConfig config;config.set(1164,i%2);runtime.setParameters(config);
        runtime.modify(ModifyReason2026::ParametersConfirmed);
        runtime.modify(ModifyReason2026::GameNotification);
        runtime.modify(ModifyReason2026::GameDialogClosed);
        check(runtime.session().journal.entries().size()==size,"repeated configuration and game events stay bounded");
    }
    const auto committed=memory;
    fault=Fault::Write;faultAt=calls[static_cast<size_t>(Fault::Write)]+2;partialFailure=true;blockRecovery=true;
    runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(fired && runtime.session().phase==SessionPhase2026::RecoveryRequired,"compacted session retains failed operation for retry");
    const auto pending=runtime.session().journal.entries().size();
    runtime.modify(ModifyReason2026::TimerTick);
    check(runtime.session().journal.entries().size()==pending,"pending recovery is never compacted");
    blockRecovery=false;
    check(runtime.retryRollback() && runtime.session().journal.entries().size()==size,"retry restores committed compact baseline");
    for(const auto& pair:memory) {
        const auto found=committed.find(pair.first);
        check(pair.second==(found==committed.end()?0:found->second),"failed operation restores last committed bytes");
    }
    check(runtime.undo(),"long-running compacted session can undo");checkRestored(before);
    resetBackend();
    std::puts("Journal overlap, resource ordering, long-running commits and recovery checks passed.");
}
void appPathsTest() {
    wchar_t module[32768]{};
    const auto length=GetModuleFileNameW(nullptr,module,_countof(module));
    check(length>0 && length<_countof(module),"test executable path available");
    const auto expected=std::filesystem::path(module);
    struct WorkingDirectory {
        std::filesystem::path previous=std::filesystem::current_path();
        ~WorkingDirectory() { std::filesystem::current_path(previous); }
    } directory;
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    const auto actual=san9::executableFile(expected.filename().c_str());
    check(actual==expected,"executable files are independent of working directory");
    std::ifstream file(actual,std::ios::binary);char signature[2]{};file.read(signature,2);
    check(file && signature[0]=='M' && signature[1]=='Z',"resolved path opens the actual executable");
    for(const wchar_t* name:{L"San9PKHardPara.txt",L"San9PKHardCity.txt",L"San9PKHardSkill.txt",
        L"San9PKHardArray.txt",L"San9PKHardPerson.txt",L"San9PKHardSettings.ini"})
        check(san9::executableFile(name)==expected.parent_path()/name,"all portable files share executable directory");
    std::puts("Portable file paths passed with a different working directory.");
}
void faultSweep() {
    resetBackend();
    Runtime2026 baseline(api()); version(baseline); initialModify(baseline);
    const auto totals=calls;
    check(baseline.undo(),"baseline undo");
    size_t cases=0;
    for(auto kind:{Fault::Open,Fault::Read,Fault::Write,Fault::Flush,Fault::Query,Fault::Protect,Fault::Allocate,Fault::Resolve,Fault::Message}) {
        const size_t count=totals[static_cast<size_t>(kind)];
        for(size_t index=1;index<=count;++index) {
            resetBackend();
            Runtime2026 runtime(api()); version(runtime); store(0x508195,0x78563412);
            const auto before=memory;
            fault=kind; faultAt=index;
            partialFailure=(kind==Fault::Write || kind==Fault::Read);
            initialModify(runtime);
            check(fired,"fault point reached");
            check(runtime.session().result==OperationResult2026::RolledBack,"failed operation rolled back");
            check(runtime.session().phase==SessionPhase2026::Idle,"failed initial session is idle");
            check(runtime.session().journal.entries().empty() && !runtime.session().process,"rollback releases ownership");
            checkRestored(before);
            check(runtime.undo(),"undo after failed modification is idempotent");
            ++cases;
        }
        std::printf("fault kind=%d: %zu points passed\n",static_cast<int>(kind),count);
    }
    std::printf("Exhaustive initial-operation fault sweep: %zu cases\n",cases);
    resetBackend();
}
void recoveryTest() {
    resetBackend();
    Runtime2026 runtime(api()); version(runtime);
    const auto before=memory;
    fault=Fault::Write;faultAt=30;partialFailure=true;shortSuccess=false;blockRecovery=true;
    initialModify(runtime);
    check(runtime.session().phase==SessionPhase2026::RecoveryRequired,"rollback failure is explicit");
    check(allocated && closes==0 && releases==0 && !runtime.session().journal.entries().empty(),"failed rollback retains resources and log");
    const auto pending=runtime.session().journal.entries().size();
    const auto previousCalls=calls;
    runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(calls==previousCalls && runtime.session().journal.entries().size()==pending,"pending recovery blocks modifications");
    check(!runtime.undo(),"failed recovery prevents close/undo success");
    blockRecovery=false;
    check(runtime.retryRollback(),"rollback retry succeeds");
    checkRestored(before);
    check(runtime.undo() && releases==1 && closes==1,"repeated undo does not release twice");
    initialModify(runtime);
    check(runtime.session().phase==SessionPhase2026::Active,"retry injection after rollback succeeds");
    check(runtime.undo(),"retried injection undo");
    checkRestored(before);
    resetBackend();
}
void liveFailureTest() {
    resetBackend();
    Runtime2026 runtime(api()); version(runtime);initialModify(runtime);
    const auto committed=memory;
    const auto journalSize=runtime.session().journal.entries().size();
    san9::ParameterConfig config;config.set(1164,1);runtime.setParameters(config);
    fault=Fault::Write;faultAt=calls[static_cast<size_t>(Fault::Write)]+15;partialFailure=true;
    runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(fired && runtime.session().result==OperationResult2026::RolledBack,"live failure rolls back one operation");
    check(runtime.session().phase==SessionPhase2026::Active && allocated && releases==0 && closes==0,"committed session retained");
    check(runtime.session().journal.entries().size()==journalSize,"committed log retained");
    for(const auto& pair:memory) {
        const auto found=committed.find(pair.first);
        check(pair.second==(found==committed.end()?0:found->second),"live rollback restores previous commit");
    }
    runtime.modify(ModifyReason2026::ParametersConfirmed);check(load(4733494,2)==59792u,"live retry commits requested configuration");
    check(runtime.undo(),"live session undo");
    resetBackend();
}
void undoFailureTest(Fault kind) {
    resetBackend();
    Runtime2026 runtime(api());version(runtime);
    const auto before=memory;
    initialModify(runtime);
    fault=kind;faultAt=calls[static_cast<size_t>(kind)]+1;partialFailure=true;
    check(!runtime.undo(),"undo failure reported");
    check(fired && runtime.session().phase==SessionPhase2026::RecoveryRequired,"undo keeps recovery state");
    check(runtime.session().process && !runtime.session().journal.entries().empty(),"undo failure retains pending ownership");
    if(kind==Fault::Write || kind==Fault::Flush) check(allocated && releases==0,"references prevent premature release");
    check(runtime.undo(),"undo retry finishes");
    check(runtime.session().journal.entries().empty() && closes==1 && releases==1,"undo cleanup exactly once");
    checkRestored(before);
    check(runtime.undo() && closes==1 && releases==1,"undo idempotence");
    resetBackend();
}
void protectionRegionTest(bool failSecondRegion) {
    resetBackend();
    Runtime2026 runtime(api());version(runtime);
    const DWORD address=RuntimeTestAccess2026{runtime}.field<DWORD>(7448);
    RuntimeTestAccess2026{runtime}.field<DWORD>(7448)=address+1; // A page-sized unaligned range spans two regions.
    protections[address]=PAGE_EXECUTE_READ;
    protections[address+0x1000]=PAGE_READWRITE;
    if(failSecondRegion) { fault=Fault::Query;faultAt=2; }
    initialModify(runtime);
    if(failSecondRegion) check(fired && runtime.session().result==OperationResult2026::RolledBack,"partial protection operation rolled back");
    else check(runtime.session().phase==SessionPhase2026::Active,"multi-region protection committed");
    check(runtime.undo(),"multi-region undo");
    check(protections[address]==PAGE_EXECUTE_READ && protections[address+0x1000]==PAGE_READWRITE,"heterogeneous original protections restored separately");
    resetBackend();
}
void windowChangeTest() {
    resetBackend();
    Runtime2026 runtime(api());version(runtime);initialModify(runtime);
    const auto count=writes;
    present=false;runtime.modify(ModifyReason2026::TimerTick);
    check(writes==count && allocated && runtime.session().process,"window disappearance cannot discard ownership or write");
    check(runtime.undo(),"cleanup uses owned process handle without window discovery");
    resetBackend();
}
void playerForceTest() {
    resetBackend();
    Runtime2026 runtime(api());version(runtime);
    constexpr DWORD flagAddress=0x1253C38+0x34;
    store(flagAddress,0x785634A4);
    check(!runtime.togglePlayerForce(0),"player force requires active session");
    initialModify(runtime);
    check(!runtime.togglePlayerForce(50),"player force index bounded");
    check(runtime.togglePlayerForce(0) && load(flagAddress)==0x785634A5,"player flag toggles one bit only");
    for(auto kind:{Fault::Read,Fault::Write,Fault::Flush}) {
        fault=kind;faultAt=calls[static_cast<size_t>(kind)]+1;fired=false;
        const auto journal=runtime.session().journal.entries().size();
        check(!runtime.togglePlayerForce(0),"player toggle failure reported");
        check(fired && load(flagAddress)==0x785634A5,"player toggle rolls back to prior commit");
        check(runtime.session().journal.entries().size()==journal,"player toggle uses same operation boundary");
    }
    check(runtime.undo() && load(flagAddress)==0x785634A4,"session undo restores original player flag");
    resetBackend();
}
size_t textCalls=0,textFailAt=0;
int WINAPI textDecode(UINT cp,DWORD flags,LPCCH source,int size,LPWSTR output,int capacity) {
    if(++textCalls==textFailAt) return 0;
    return ::MultiByteToWideChar(cp,flags,source,size,output,capacity);
}
int WINAPI textMap(LCID locale,DWORD flags,LPCWSTR source,int size,LPWSTR output,int capacity) {
    if(++textCalls==textFailAt) return 0;
    return ::LCMapStringW(locale,flags,source,size,output,capacity);
}
int WINAPI textEncode(UINT cp,DWORD flags,LPCWCH source,int size,LPSTR output,int capacity,LPCCH fallback,LPBOOL used) {
    if(++textCalls==textFailAt) return 0;
    return ::WideCharToMultiByte(cp,flags,source,size,output,capacity,fallback,used);
}
int WINAPI expandingMap(LCID,DWORD,LPCWSTR,int,LPWSTR output,int capacity) {
    if(output && capacity>=4) for(int i=0;i<4;++i) output[i]=L'A';
    return 4;
}
int WINAPI substitutingEncode(UINT,DWORD,LPCWCH,int,LPSTR,int,LPCCH,LPBOOL used) {
    *used=TRUE;return 1;
}
void stateBoundsTest() {
    Runtime2026 runtime(api());BYTE value=0xAB,result=0;
    const size_t end=san9::layout2026::StateSize;
    RuntimeTestAccess2026{runtime}.copyIn(end-1,&value,1);RuntimeTestAccess2026{runtime}.copyOut(end-1,&result,1);
    check(result==value,"last state byte accessible");
    RuntimeTestAccess2026{runtime}.copyIn(end,&value,0);RuntimeTestAccess2026{runtime}.copyOut(end,&result,0);
    const auto rejected=[](const std::function<void()>& operation) {
        try {operation();} catch(const std::out_of_range&) {return true;}
        return false;
    };
    const size_t maximum=static_cast<size_t>(-1);
    check(rejected([&] {RuntimeTestAccess2026{runtime}.field<DWORD>(maximum-1);}),"field offset addition cannot wrap");
    check(rejected([&] {RuntimeTestAccess2026{runtime}.field<DWORD>(end-1);}),"field width bounded");
    check(rejected([&] {RuntimeTestAccess2026{runtime}.copyIn(maximum,&value,2);})
        && rejected([&] {RuntimeTestAccess2026{runtime}.copyOut(maximum,&result,2);}),"copy offset addition cannot wrap");
    check(rejected([&] {RuntimeTestAccess2026{runtime}.copyIn(1,&value,maximum);})
        && rejected([&] {RuntimeTestAccess2026{runtime}.copyOut(1,&result,maximum);}),"copy size addition cannot wrap");
    std::puts("State buffer boundary and overflow checks passed.");
}
void imageBoundsTest() {
    resetBackend();
    Runtime2026 runtime(api());
    auto access=RuntimeTestAccess2026{runtime};
    constexpr DWORD first=0x41F000,end=0x430000,record=0x42FF00,remote=0x600000;
    const auto maximum=(std::numeric_limits<size_t>::max)();
    const auto rejected=[](const std::function<void()>& action) {
        try { action(); } catch(const std::out_of_range&) { return true; }
        return false;
    };
    BYTE value=0xAB,result=0;
    access.dataIn(end-1,&value,1); access.dataOut(end-1,&result,1);
    check(result==value && *access.source(end-1,1)==value,"last image byte can be copied and resolved");
    access.dataIn(end,nullptr,0); access.dataOut(end,nullptr,0);
    check(access.source(end,0)==access.source(end-1,1)+1,"empty companion range is valid at the end");
    check(rejected([&]{access.source(end,1);}) && rejected([&]{access.source(end-1,2);})
        && rejected([&]{access.source(first-1,1);}) && rejected([&]{access.source(first,maximum);})
        && rejected([&]{access.pointer<DWORD>(0xFFFFFFFE);}),
        "companion ranges reject crossings, underflow and address addition wrap");
    check(rejected([&]{access.dataIn(end-1,&value,2);}) && rejected([&]{access.dataOut(end-1,&result,2);})
        && rejected([&]{access.dataIn(0xFFFFFFFF,&value,maximum);}),
        "full copy ranges fail before accessing either buffer");
    const auto native=reinterpret_cast<std::uintptr_t>(access.source(first,1));
    check(access.nativeSource(native+end-first,0)==access.source(end,0),"empty native image range is valid at the end");
    check(rejected([&]{access.nativeSource(native+end-first-1,2);})
        && rejected([&]{access.nativeSource(native-1,1);})
        && rejected([&]{access.nativeSource((std::numeric_limits<std::uintptr_t>::max)(),8);}),
        "native image sources reject forged pointers and full-length overflow");
    check(imageRecordBytes2026(0,12)==0 && imageRecordBytes2026(2,12)==24
        && rejected([&]{imageRecordBytes2026(maximum,12);}),"record table sizes cannot multiply-wrap");
    access.descriptorRange(end,0,PatchSource2026::CompanionAddress);
    access.inlineRange(end,0);
    check(reads==0 && writes==0,"empty record tables do not access the process");
    version(runtime); initialModify(runtime);
    store(remote,0x12345678); store(remote+4,0xABCDEF01);
    const BYTE replacement[8]={8,7,6,5,4,3,2,1};
    for(unsigned scenario=0;scenario<14;++scenario) {
        PatchDescriptor2026 descriptor{remote+32,8,end-4};
        if (scenario==1) descriptor.source=static_cast<DWORD>(native+end-first-4);
        if (scenario==2) { descriptor.source=0xFFFFFFFE; descriptor.length=4; }
        if (scenario==3) { descriptor.source=1; descriptor.length=1; }
        if (scenario==12) descriptor.length=0;
        if (scenario==13) descriptor.source=first-1;
        access.dataIn(record,&descriptor,sizeof(descriptor));
        InlinePatch2026 inlined{remote+32,9,{}};
        access.dataIn(record+32,&inlined,sizeof(inlined));
        const auto before=memory;
        const auto originalCalls=calls;
        const auto beforeWrites=writes,beforeReads=reads,beforeJournal=runtime.session().journal.size();
        check(!access.operation([&] {
            access.write(remote,replacement,sizeof(replacement));
            switch(scenario) {
            case 0: case 2: case 12: case 13: access.descriptor(record,PatchSource2026::CompanionAddress);break;
            case 1: case 3: access.descriptor(record,PatchSource2026::HostPointer);break;
            case 4: access.descriptor(end-11,PatchSource2026::CompanionAddress);break;
            case 5: access.descriptorRange(end-12,2,PatchSource2026::CompanionAddress);break;
            case 6: access.descriptorRange(first,maximum,PatchSource2026::CompanionAddress);break;
            case 7: access.inlineRange(record+32,1);break;
            case 8: access.capture(remote,end-4,8);break;
            case 9: access.write(remote+32,access.stateBytes(san9::layout2026::StateSize-1,2),2);break;
            case 10: access.write(remote+32,access.source(end-4,4),8);break;
            case 11: access.read(remote,access.pointer<BYTE>(end-4),8);break;
            }
        }),"invalid image access aborts the transaction");
        check(memory==before && runtime.session().journal.size()==beforeJournal && runtime.hasGameSession()
            && runtime.session().result==OperationResult2026::RolledBack,
            "range failure restores prior writes and retains the committed session");
        check(writes==beforeWrites+2 && reads==beforeReads+1,
            "invalid image source or capture destination reaches no remote transfer");
        check(calls[static_cast<size_t>(Fault::Allocate)]==originalCalls[static_cast<size_t>(Fault::Allocate)]
            && allocated && closes==0 && releases==0,"range failure retains session resources");
    }
    store(remote,0x11223344);
    const auto readBefore=reads;
    check(access.operation([&]{access.capture(remote,end-4,4);}),"valid old-byte capture accepts the complete end range");
    DWORD captured=0;access.dataOut(end-4,&captured,sizeof(captured));
    check(captured==0x11223344 && reads==readBefore+1,"capture reads exactly the requested bytes into the image");
    auto before=memory;
    fault=Fault::Read;faultAt=calls[static_cast<size_t>(Fault::Read)]+2;
    partialFailure=true;shortSuccess=false;
    check(!access.operation([&] {
        access.write(remote,replacement,sizeof(replacement));
        access.capture(remote,end-4,4);
    }),"partial dynamic capture aborts its transaction");
    DWORD restored=0;access.dataOut(end-4,&restored,sizeof(restored));
    check(restored==captured && memory==before && runtime.hasGameSession(),
        "partial capture restores both host bytes and earlier game writes");
    check(runtime.status().failure.operation==RuntimeOperation2026::Read
        && runtime.status().failure.target==remote && runtime.status().failure.requested==4
        && runtime.status().failure.transferred==3,"partial capture keeps its transfer diagnostic");
    fault=Fault::Write;fired=false;partialFailure=false;
    faultAt=calls[static_cast<size_t>(Fault::Write)]+2;blockRecovery=true;
    PatchDescriptor2026 invalid{remote+32,8,end-4};access.dataIn(record,&invalid,sizeof(invalid));
    const auto committedBoundary=runtime.session().journal.size();
    check(!access.operation([&] {
        access.write(remote,replacement,sizeof(replacement));
        access.descriptor(record,PatchSource2026::CompanionAddress);
    }),"range failure with failed restoration retains recovery state");
    check(runtime.session().phase==SessionPhase2026::RecoveryRequired
        && runtime.session().journal.size()==committedBoundary+1
        && runtime.status().failure.operation==RuntimeOperation2026::RuntimeException
        && runtime.status().recoveryFailure.operation==RuntimeOperation2026::RestoreBytes,
        "local range failure and remote recovery failure retain separate causes and pending bytes");
    blockRecovery=false;
    check(runtime.retryRollback() && memory==before && runtime.session().journal.size()==committedBoundary,
        "range failure recovery retry restores the committed image and journal boundary");
    check(runtime.undo(),"image-boundary test session cleans up");
    resetBackend();
    std::puts("Complete image ranges, record overflow, capture bounds and transaction recovery passed.");
}
void gameTextTest() {
    using san9::TextResult;
    char empty[1]={0};check(san9::simplifyGameText(empty,1)==TextResult::success,"empty game name");
    char ascii[]="ABC";check(san9::simplifyGameText(ascii,sizeof(ascii))==TextResult::success && std::strcmp(ascii,"ABC")==0,"ASCII exact capacity");
    char unterminated[2]={'A','B'};
    check(san9::simplifyGameText(nullptr,1)==TextResult::invalidBuffer
        && san9::simplifyGameText(ascii,0)==TextResult::invalidBuffer
        && san9::simplifyGameText(unterminated,2)==TextResult::unterminated && unterminated[1]=='B',"invalid buffers never read beyond capacity");
    char invalid[]={char(0x81),0};
    check(san9::simplifyGameText(invalid,sizeof(invalid))==TextResult::decodeFailed && BYTE(invalid[0])==0x81,"incomplete Big5 sequence rejected atomically");
    char original[16]{};char expected[16]{};
    check(::WideCharToMultiByte(950,0,L"\u570B",-1,original,sizeof(original),nullptr,nullptr)>0,"traditional fixture encoding");
    check(::WideCharToMultiByte(936,0,L"\u56FD",-1,expected,sizeof(expected),nullptr,nullptr)>0,"simplified expected encoding");
    auto converted=std::array<char,16>{};std::memcpy(converted.data(),original,sizeof(original));
    check(san9::simplifyGameText(converted.data(),converted.size())==TextResult::success
        && std::strcmp(converted.data(),expected)==0,"Big5 traditional to GBK simplified");
    const san9::TextApi faultApi{textDecode,textMap,textEncode};
    for(size_t fail=1;fail<=6;++fail) {
        textCalls=0;textFailAt=fail;std::memcpy(converted.data(),original,sizeof(original));
        check(san9::simplifyGameText(converted.data(),converted.size(),faultApi)!=TextResult::success
            && std::memcmp(converted.data(),original,sizeof(original))==0 && textCalls==fail,"each conversion API failure preserves input");
    }
    char smallBuffer[]="A";san9::TextApi expanded;expanded.simplify=expandingMap;
    check(san9::simplifyGameText(smallBuffer,sizeof(smallBuffer),expanded)==TextResult::insufficientCapacity && std::strcmp(smallBuffer,"A")==0,"expanded conversion does not truncate");
    san9::TextApi lossy;lossy.encode=substitutingEncode;
    check(san9::simplifyGameText(smallBuffer,sizeof(smallBuffer),lossy)==TextResult::unrepresentable && std::strcmp(smallBuffer,"A")==0,"default-character substitution is explicit failure");
    // The officer table snapshot is read-only game data. Converting it must never
    // rewrite the Big5 bytes, otherwise the second render decodes GBK as Big5.
    char traditional[16]{};
    check(::WideCharToMultiByte(950,0,L"\u570B\u6C11",-1,traditional,sizeof(traditional),nullptr,nullptr)>0,"traditional snapshot fixture encoding");
    char snapshotCopy[16]{};std::memcpy(snapshotCopy,traditional,sizeof(traditional));
    const auto wide=san9::gameWideText(traditional,sizeof(traditional));
    check(wide==L"\u56FD\u6C11","Big5 snapshot converts to wide simplified Chinese");
    check(std::memcmp(snapshotCopy,traditional,sizeof(traditional))==0,"wide conversion leaves the snapshot bytes untouched");
    check(san9::gameWideText(traditional,sizeof(traditional))==wide,"repeated conversion of the same snapshot is stable");
    // A destructive conversion would have replaced the Big5 bytes with GBK, so the
    // original text must still decode to the same result afterwards.
    check(san9::gameWideText(traditional,sizeof(traditional))==L"\u56FD\u6C11","snapshot is still decodable as Big5 after conversion");
    char probe[16]{};std::memcpy(probe,traditional,sizeof(traditional));
    check(san9::simplifyGameText(probe,sizeof(probe))==TextResult::success && std::memcmp(probe,traditional,sizeof(traditional))!=0
        && san9::gameWideText(traditional,sizeof(traditional))==L"\u56FD\u6C11","in-place conversion of a copy does not corrupt the snapshot");
    char truncated[2]={'A','B'};
    check(san9::gameWideText(nullptr,1).empty()
        && san9::gameWideText(traditional,0).empty()
        && san9::gameWideText(truncated,sizeof(truncated)).empty()
        && truncated[1]=='B',"wide conversion rejects invalid buffers without reading beyond capacity");
    char invalidWide[]={char(0x81),0};
    check(san9::gameWideText(invalidWide,sizeof(invalidWide)).empty() && BYTE(invalidWide[0])==0x81,"wide conversion rejects an incomplete Big5 sequence");
    std::puts("Game text encoding, capacity and six API failure checks passed.");
}
void personSnapshotTest() {
    san9::PersonSnapshot snapshot;
    check(!snapshot.nextAbility(0,0,80),"offline search has no candidates");
    std::vector<BYTE> bytes(san9::PersonSnapshot::Count*san9::PersonSnapshot::RecordSize,0);
    auto* last=bytes.data()+799*san9::PersonSnapshot::RecordSize;
    std::memcpy(last+0x10,"ABCDEWXYZ",9); // Neither field has a terminator in its payload.
    last[0x84]=255;last[0x64]=255;last[0x4C]=128;last[0x54]=90;last[0x5C]=81;
    last[0xE8]=0xFF;last[0xE9]=0xFF;last[0xA4]=0xFF;last[0xA5]=0xFF;
    last[0xB4]=0x34;last[0xB5]=0x12;
    last[0x100]=0x12;last[0x101]=0x34;last[0x102]=0x56;last[0x103]=0x78;
    bytes[0x10]='A';bytes[0x11]=0;bytes[0x15]='B';bytes[0x84]=9;bytes[0x64]=253;
    bytes[san9::PersonSnapshot::RecordSize+0x64]=255;
    check(snapshot.decode(bytes.data(),bytes.size()) && snapshot.size()==800,"complete snapshot decodes");
    const auto& row=snapshot.at(799);
    check(std::strcmp(row.name.data(),"ABCDWXYZ")==0 && row.name.back()==0
        && std::strcmp(snapshot.at(0).name.data(),"AB")==0,"bounded name fields concatenate and terminate");
    check(row.status==10 && snapshot.at(0).status==9 && row.abilities[0]==255 && row.abilities[1]==128,
        "status bounds and unsigned abilities");
    check(row.good==0x1FF && row.proficiency[0]==65535 && row.proficiency[8]==0x1234 && row.learned==0x78563412,
        "snapshot skill fields use explicit little-endian decode");
    check(!snapshot.decode(nullptr,bytes.size()) && !snapshot.decode(bytes.data(),bytes.size()-1)
        && !snapshot.decode(bytes.data(),bytes.size()+1) && snapshot.at(799).learned==0x78563412,"invalid snapshot size preserves previous data");
    const auto wrapped=snapshot.nextAbility(799,0,255);
    check(wrapped && *wrapped==0,"ability search wraps and includes tolerance boundary");
    const auto next=snapshot.nextAbility(*wrapped,0,255);
    check(next && *next==1,"repeated search uses original target");
    check(!snapshot.nextAbility(800,0,80) && !snapshot.nextAbility(0,4,80) && !snapshot.nextAbility(0,0,-1)
        && !snapshot.nextAbility(0,0,256) && !snapshot.nextAbility(799,1,128),"invalid search and self-only matches terminate");
    const auto named=snapshot.next(0,[](const san9::PersonInfo& value) {return std::string(value.name.data()).find("WXYZ")!=std::string::npos;});
    check(named && *named==799,"text search reaches last record");
    for(const auto pair:std::array<std::pair<int,int>,12>{{{0,5},{30,5},{31,2},{50,2},{51,1},{60,1},{61,0},{80,0},{81,1},{90,1},{91,2},{255,2}}})
        check(san9::PersonSnapshot::abilityTolerance(pair.first)==pair.second,"sample search tolerance thresholds");
    std::puts("Person snapshot decoding, boundaries and bounded search checks passed.");
}
void personConfigTest(const char* path) {
    san9::PersonConfig config;
    check(config.valid() && config.levels.back()==0x3FEAA,"person defaults");
    std::ifstream fixture(path);check(bool(fixture),"person fixture exists");
    check(config.read(fixture).error==san9::PackedReadError::none,"916 person lines load");
    const auto levels=config.encodeLevels();const auto friendships=config.encodeFriendships();
    std::ostringstream output;check(config.write(output),"person save");
    san9::PersonConfig copy;std::istringstream input(output.str());
    check(copy.read(input).error==san9::PackedReadError::none && copy.encodeLevels()==levels
        && copy.encodeFriendships()==friendships,"person round trip");
    auto replaceLine=[&](size_t index,const char* value) {
        std::istringstream original(output.str());std::ostringstream changed;std::string line;size_t current=0;
        while(std::getline(original,line)) {changed<<(current++==index?value:line.c_str())<<'\n';}
        return changed.str();
    };
    for(const auto& invalid:std::vector<std::string>{"", "\n", "12bad\n", "2147483648\n", "-1\n",
        replaceLine(799,"262144"),replaceLine(800,"800"),replaceLine(915,"63963136"),replaceLine(915,"67108864"),
        output.str()+"1\n"}) {
        std::istringstream bad(invalid);
        check(config.read(bad).error!=san9::PackedReadError::none && config.encodeLevels()==levels
            && config.encodeFriendships()==friendships,"invalid person file never partially commits");
    }
    config.levels.back()=0x3FFFF;config.friendships.back()={799,798,60};
    const auto encoded=config.encodeFriendships();
    check(encoded[690]==0x1F && encoded[691]==3 && encoded[692]==0x1E && encoded[693]==3
        && encoded[694]==60 && encoded[695]==0,"friendships encode three little-endian WORDs");
    san9::PersonConfig decoded;decoded.decode(config.encodeLevels(),encoded);
    check(decoded.valid() && decoded.levels.back()==0x3FFFF && decoded.friendships.back()==config.friendships.back(),"person runtime byte round trip");
    decoded.friendships.back()[0]=800;std::ostringstream unchanged;unchanged<<"keep";
    check(!decoded.write(unchanged) && unchanged.str()=="keep","invalid person save writes nothing");
    resetBackend();Runtime2026 runtime(api());
    check(runtime.personConfig().valid(),"sample person defaults valid");
    runtime.setPersonConfig(config);version(runtime);initialModify(runtime);
    check(load(RuntimeTestAccess2026{runtime}.field<DWORD>(3700)+799*4)==0x3FFFF,"last person's nine limits applied");
    check(load(RuntimeTestAccess2026{runtime}.field<DWORD>(4400)+115*6,2)==799 && load(RuntimeTestAccess2026{runtime}.field<DWORD>(4400)+115*6+2,2)==798
        && load(RuntimeTestAccess2026{runtime}.field<DWORD>(4400)+115*6+4,2)==60,"last friendship applied as three words");
    config.levels[0]=0;config.friendships[0]={1,2,3};
    runtime.setPersonConfig(config);runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(load(RuntimeTestAccess2026{runtime}.field<DWORD>(3700))==0 && load(RuntimeTestAccess2026{runtime}.field<DWORD>(4400)+4,2)==3,
        "confirmed person settings apply on parameter update");
    check(runtime.undo(),"person settings use journal undo");
    resetBackend();Runtime2026 exiting(api());version(exiting);initialModify(exiting);exiting.setPersonConfig(config);
    processAlive=false;allocated=false;exiting.modify(ModifyReason2026::TimerTick);
    const auto preserved=exiting.personConfig();
    check(preserved.encodeLevels()==config.encodeLevels() && preserved.encodeFriendships()==config.encodeFriendships(),
        "exit preserves full person and 696-byte friendship tables");
    resetBackend();std::puts("Person file, encoding, application and exit preservation checks passed.");
}
void formationConfigTest(const char* path) {
    san9::FormationConfig config;
    check(config.valid(),"offline formation defaults valid");
    std::ifstream fixture(path);check(bool(fixture),"formation fixture exists");
    check(config.read(fixture).error==san9::PackedReadError::none,"54 formation lines load");
    const auto bytes=config.encode();
    check(config.entries[0].troop==15 && config.entries[0].range==2 && config.entries[0].expense==150
        && config.entries[26].range==1 && config.entries[26].garrison==30,"formation packed layout");
    std::ostringstream output;check(config.write(output),"formation save");
    san9::FormationConfig copy;std::istringstream input(output.str());
    check(copy.read(input).error==san9::PackedReadError::none && copy.encode()==bytes,"formation round trip");
    auto invalid=output.str();invalid.replace(0,invalid.find('\n'),"2147483647");
    for(const auto& bad:std::vector<std::string>{"203989967\n", "\n", "12oops\n", "2147483648\n",invalid,output.str()+"1\n"}) {
        std::istringstream stream(bad);
        check(config.read(stream).error!=san9::PackedReadError::none && config.encode()==bytes,"formation invalid input is atomic");
    }
    auto badBytes=bytes;badBytes[4]|=0x0F;copy.decode(badBytes);
    std::ostringstream untouched;untouched<<"keep";
    check(!copy.write(untouched) && untouched.str()=="keep","invalid formation save writes nothing");
    std::array<BYTE,san9::FormationConfig::GameRecordSize> game{};
    game[0x60]=50;game[0x64]=50;game[0x68]=60;game[0x6C]=30;game[0x5C]=50;
    game[0x70]=10;game[0x74]=8;game[0x78]=0x10;game[0x79]=0x27;
    copy=config;copy.entries.back().range=5;
    check(copy.importGame(26,game.data(),game.size()) && copy.entries.back().range==5
        && copy.entries.back().expense==10000,"game decode preserves companion range");
    const auto imported=copy.encode();game[0x60]=0xFF;
    check(!copy.importGame(26,game.data(),game.size()) && copy.encode()==imported,"invalid game value does not truncate or commit");
    check(!copy.importGame(27,game.data(),game.size()) && !copy.importGame(0,game.data(),1)
        && !copy.importGame(0,nullptr,game.size()),"formation input bounds");
    resetBackend();Runtime2026 runtime(api());version(runtime);initialModify(runtime);
    const auto seeded=runtime.formationConfig();
    check(seeded.valid() && seeded.entries[10].range==3 && seeded.entries[26].range==1
        && seeded.entries[0].angle==1,"range initialization retains valid view defaults");
    config.entries[26]={50,50,60,30,50,5,10,8,10000};
    runtime.setFormationConfig(config);
    const DWORD target=0x1500B70+26*0x88;
    for(DWORD offset:{0x5Cu,0x60u,0x64u,0x68u,0x6Cu,0x70u,0x74u,0x78u}) store(target+offset,0xAABBCCDD);
    RuntimeTestAccess2026{runtime}.field<BYTE>(san9::layout2026::FormationFlags)=2;runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(RuntimeTestAccess2026{runtime}.field<BYTE>(san9::layout2026::FormationFlags)==0,"formation pending consumed");
    for(const auto pair:std::array<std::pair<DWORD,BYTE>,7>{{{0x60,50},{0x64,50},{0x68,60},{0x6C,30},{0x5C,50},{0x70,10},{0x74,8}}})
        check(load(target+pair.first)==(0xAABBCC00u|pair.second),"formation writes one byte and preserves neighbors");
    check(load(target+0x78)==0xAABB2710 && load(RuntimeTestAccess2026{runtime}.field<DWORD>(5212)+26,1)==5,"expense word and independent range table");
    RuntimeTestAccess2026{runtime}.field<BYTE>(san9::layout2026::FormationFlags)=3;runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(RuntimeTestAccess2026{runtime}.field<BYTE>(san9::layout2026::FormationFlags)==1,"formation file flag persists");
    check(runtime.undo() && load(target+0x78)==0xAABBCCDD,"formation changes undo through journal");
    resetBackend();Runtime2026 exiting(api());version(exiting);initialModify(exiting);
    exiting.setFormationConfig(config);RuntimeTestAccess2026{exiting}.field<BYTE>(san9::layout2026::FormationFlags)=1;
    processAlive=false;allocated=false;exiting.modify(ModifyReason2026::TimerTick);
    check(exiting.formationConfig().encode()==config.encode() && RuntimeTestAccess2026{exiting}.field<BYTE>(san9::layout2026::FormationFlags)==3,"exit preserves all 216 formation bytes and requeues file settings");
    resetBackend();
    std::puts("Formation format, invalid input, game decoding, application and exit preservation checks passed.");
}
void skillConfigTest(const char* path) {
    san9::SkillConfig config;
    std::ifstream fixture(path);check(bool(fixture),"skill fixture exists");
    check(config.read(fixture).error==san9::PackedReadError::none,"41 skill entries load");
    const auto bytes=config.encode();
    check(bytes[0]==13 && bytes[1]==9 && bytes[2]==150 && bytes[3]==0,"skill DWORD byte order");
    std::ostringstream output;check(config.write(output),"skill save");
    san9::SkillConfig copy;std::istringstream input(output.str());
    check(copy.read(input).error==san9::PackedReadError::none && copy.encode()==bytes,"skill round trip");
    for(const auto& bad:std::vector<std::string>{"1\n","\n","12oops\n","2147483648\n","255\n",output.str()+"1\n"}) {
        std::istringstream stream(bad);
        check(config.read(stream).error!=san9::PackedReadError::none && config.encode()==bytes,"invalid skill input never partially commits");
    }
    std::array<BYTE,san9::SkillConfig::GameRecordSize> game{};
    game[0x64]=100;game[0x7C]=30;game[0x58]=0xC4;game[0x59]=9;
    check(copy.importGame(40,game.data(),game.size()) && copy.entries.back().proficiency==2500,"skill game offsets and last entry");
    check(!copy.importGame(41,game.data(),game.size()) && !copy.importGame(0,game.data(),1),"skill input bounds");
    resetBackend();Runtime2026 runtime(api());version(runtime);initialModify(runtime);
    san9::ParameterConfig invalidParameters;invalidParameters.set(1020,-1);
    const auto beforeParameters=RuntimeTestAccess2026{runtime}.field<int>(san9::ParameterConfig::fields()[0].hostOffset);
    bool rejected=false;
    try { runtime.setParameters(invalidParameters); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected && RuntimeTestAccess2026{runtime}.field<int>(san9::ParameterConfig::fields()[0].hostOffset)==beforeParameters,"runtime rejects invalid parameters before any field changes");
    config.entries[0]={100,30,2500};runtime.setSkillConfig(config);RuntimeTestAccess2026{runtime}.field<BYTE>(san9::layout2026::SkillFlags)=2;runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(RuntimeTestAccess2026{runtime}.field<BYTE>(san9::layout2026::SkillFlags)==0,"one-shot skill pending flag consumed");
    check(load(0x14FDAD0+0x64)==100 && load(0x14FDAD0+0x7C)==30 && load(0x14FDAD0+0x58)==2500,"skill application matches UI limits");
    RuntimeTestAccess2026{runtime}.field<BYTE>(san9::layout2026::SkillFlags)=3;runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(RuntimeTestAccess2026{runtime}.field<BYTE>(san9::layout2026::SkillFlags)==1,"file-loaded flag persists after application");
    check(runtime.undo(),"skill edits use session undo");resetBackend();
    std::puts("Skill format, invalid input, game decoding and application checks passed.");
}
void cityConfigTest(const char* path) {
    san9::CityConfig config;
    std::ifstream fixture(path);
    check(bool(fixture),"city fixture exists");
    check(config.read(fixture).error==san9::CityReadError::none,"supplied 250-line city config loads");
    const auto bytes=config.encodeLimits();
    const BYTE first[]={0,50,6,10,6,4};
    check(std::memcmp(bytes.data(),first,6)==0,"first city's six bytes match input DWORD/WORD layout");
    std::ostringstream output;check(config.write(output),"canonical city serialization");
    std::istringstream linesInput(output.str());std::vector<std::string> lines;std::string line;
    while(std::getline(linesInput,line)) lines.push_back(line);
    check(lines.size()==250 && std::stoul(lines.at(1))==0x0406,"second line excludes next city's bytes");
    check(std::stoul(lines.at(99))<=65535,"last city's second line reads only its two bytes");
    san9::CityConfig restored;std::istringstream serialized(output.str());
    check(restored.read(serialized).error==san9::CityReadError::none && restored.encodeLimits()==bytes
        && restored.relations==config.relations && restored.attackDays==config.attackDays,"city round trip preserves fields");
    auto reject=[&](const std::string& text,san9::CityReadError error) {
        std::istringstream input(text);
        check(config.read(input).error==error,"city invalid input classified");
        check(config.encodeLimits()==bytes && config.relations==restored.relations
            && config.attackDays==restored.attackDays,"city parse failure leaves every field unchanged");
    };
    reject("1\n",san9::CityReadError::missingLine);
    reject("\n",san9::CityReadError::invalidInteger);
    reject("12oops\n",san9::CityReadError::invalidInteger);
    reject("2147483648\n",san9::CityReadError::integerOverflow);
    reject("0\n"+output.str().substr(output.str().find('\n')+1),san9::CityReadError::outOfRange);
    reject(output.str()+"99\n",san9::CityReadError::extraData);
    std::array<BYTE,san9::CityConfig::GameRecordSize> record{};
    auto put=[&](size_t at,DWORD value) {for(unsigned i=0;i<4;++i) record.at(at+i)=static_cast<BYTE>(value>>(8*i));};
    put(0x51,2);put(0x54,0x180);put(0x1E0,1);put(0x1C8,1200);put(0x1D4,1100);put(0x1D8,900);
    check(restored.importGameCity(49,record.data(),record.size()),"bounded unaligned game fields decode");
    const auto& last=restored.limits.back();
    check(last.flags==0xB0 && last.durability==12 && last.income==11 && last.harvest==9,"game field offsets and flags");
    check(!restored.importGameCity(50,record.data(),record.size()) && !restored.importGameCity(0,record.data(),1),"city index and record size bounded");
    put(0x51,0);
    check(restored.importGameCity(49,record.data(),record.size()) && (restored.limits.back().flags&15)==11,"no city skill maps to twelfth combo item");
    resetBackend();
    Runtime2026 runtime(api());version(runtime);runtime.setCityConfig(config);
    RuntimeTestAccess2026{runtime}.field<DWORD>(4416)=1;initialModify(runtime);
    check(RuntimeTestAccess2026{runtime}.field<DWORD>(4416)==1,"loaded city file does not enable automatic application");
    config.limits[0].durability=20;config.limits[0].income=15;config.limits[0].harvest=15;
    runtime.setCityConfig(config);RuntimeTestAccess2026{runtime}.field<DWORD>(4412)=1;runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(RuntimeTestAccess2026{runtime}.field<DWORD>(4416)==0,"successful automatic application consumes pending flag");
    check(load(0x124DB58+0x1C8)==2000 && load(0x124DB58+0x1D4)==1500
        && load(0x124DB58+0x1D8)==1500,"city application matches UI limits");
    // A new scenario/save can replace city records after the pending flag was consumed.
    constexpr DWORD durability=0x124DB58+0x1C8;
    for(auto reason:{ModifyReason2026::GameNotification,ModifyReason2026::GameDialogClosed}) {
        store(durability,600);
        runtime.modify(reason);
        check(load(durability)==2000,"automatic city configuration reapplies on game events without pending edits");
        check(RuntimeTestAccess2026{runtime}.field<DWORD>(4416)==0,"reapplication does not require rearming pending state");
    }
    store(durability,600);
    runtime.modify(ModifyReason2026::TimerTick);
    check(load(durability)==600,"ordinary timer ticks do not overwrite city data");
    RuntimeTestAccess2026{runtime}.field<DWORD>(4412)=0;
    for(auto reason:{ModifyReason2026::GameNotification,ModifyReason2026::GameDialogClosed}) {
        runtime.modify(reason);
        check(load(durability)==600,"disabled automatic city configuration remains untouched on game events");
    }
    RuntimeTestAccess2026{runtime}.field<DWORD>(4412)=1;
    runtime.modify(ModifyReason2026::GameNotification);
    check(load(durability)==2000,"reenabling automatic application does not require pending edits");
    constexpr DWORD merchant=0x124DB58+0x1E0;
    store(merchant,0x785634A4);
    for(bool enabled:{true,false,true,false}) {
        config.limits[0].flags=static_cast<BYTE>((config.limits[0].flags&~0x80u)|(enabled?0x80u:0));
        runtime.setCityConfig(config);RuntimeTestAccess2026{runtime}.field<DWORD>(4416)=1;runtime.modify(ModifyReason2026::ParametersConfirmed);
        check(load(merchant)==(enabled?0x785634A5u:0x785634A4u),"merchant setting toggles bit zero without changing other bits or adjacent bytes");
    }
    // All 12 x 12 transitions, including "none", must replace the whole selection.
    constexpr DWORD skillAddress=0x124DB58+0x51;
    constexpr DWORD skillMask=0x7F800E;
    const auto skillBit=[](unsigned index) -> DWORD { return index==11?0u:1u<<(index<3?index+1:index+12); };
    for(unsigned from=0;from<12;++from) for(unsigned to=0;to<12;++to) {
        constexpr DWORD unrelated=0x807FF1;
        store(skillAddress,unrelated|skillBit(from));
        config.limits[0].flags=static_cast<BYTE>((config.limits[0].flags&0xF0u)|to);
        runtime.setCityConfig(config);runtime.modify(ModifyReason2026::ParametersConfirmed);
        check(load(skillAddress,3)==(unrelated|skillBit(to)),"city skill transition preserves unrelated bits and clears old skills");
        check((load(skillAddress,3)&skillMask)==skillBit(to),"exactly the selected city skill remains");
    }
    check(runtime.undo(),"city application remains in session journal");
    resetBackend();
    std::puts("City format, invalid input, bounded game decoding and application checks passed.");
}
void personalizationConfigTest() {
    OfficerPersonalization2026 config;
    check(config.proficiencyCap[0][0]==1000 && config.activationBoost[0]==0
        && config.powerPercent[0]==0 && config.criticalHit[0]==0,"personalization defaults");
    check(config.interval[0][0]==9 && config.interval[0][23]==6 && config.interval[0][26]==15
        && config.interval[0][12]==0,"interval defaults to game originals");
    config.proficiencyCap[0][0]=1200;config.proficiencyCap[0][8]=65535;
    config.interval[0][1]=3;config.interval[799][38]=0;
    config.activationBoost[5]=1;config.powerPercent[0]=120;config.criticalHit[7]=1;
    std::ostringstream output;check(config.write(output),"personalization save");
    OfficerPersonalization2026 copy;std::istringstream input(output.str());
    check(copy.read(input).error==san9::PackedReadError::none,"personalization round trip");
    check(copy.proficiencyCap[0][0]==1200 && copy.proficiencyCap[0][8]==65535 &&
          copy.interval[0][1]==3 && copy.interval[799][38]==0 &&
          copy.activationBoost[5]==1 && copy.powerPercent[0]==120 && copy.criticalHit[7]==1,
          "personalization round-trip values");
    std::string officer;
    for (int i=0;i<50;++i) officer+="0\n"; // 9 caps + 41 intervals
    for(const auto& invalid:std::vector<std::string>{
        "65536\n", "0\n0\n0\n0\n0\n0\n0\n0\n0\n31\n", officer+"2\n", officer+"0\n251\n", officer+"0\n0\n2\n"}) {
        std::istringstream bad(invalid);
        OfficerPersonalization2026 before=config;
        check(config.read(bad).error!=san9::PackedReadError::none,"invalid personalization rejected");
        check(before.proficiencyCap==config.proficiencyCap && before.interval==config.interval
            && before.activationBoost==config.activationBoost && before.powerPercent==config.powerPercent
            && before.criticalHit==config.criticalHit,
            "invalid personalization never partially commits");
    }
    std::puts("Personalization format, invalid input and round-trip checks passed.");
}
// The four per-officer injection blocks derive the officer index from the
// person record itself: "movzx reg, word [ptr+4]" (CPersonData.id, the
// person-table index 0..849). The broken 2019-era derivation used
// (ptr - 0x1258EE0) / 0x128, which requires the pointer to be the officer
// table base; every caller hands a different object, so that form dereferenced
// garbage and crashed. These assertions guard the encoded bytes themselves,
// because the offline tests never execute the injected code.
void injectionCodeBytesTest() {
    resetBackend();
    Runtime2026 runtime(api());
    version(runtime);
    GrowthBoost2026 growth;
    growth.officerList=L"5";
    growth.multiplier=2;
    runtime.setGrowthBoost(growth);
    OfficerPersonalization2026 personalization;
    personalization.activationBoost[0]=1;
    personalization.criticalHit[0]=1;
    personalization.interval[0][0]=12;
    runtime.setPersonalization(personalization);
    recordWrites=true;
    initialModify(runtime);
    recordWrites=false;

    // Each payload writes the person-table index via "movzx r16, word [ptr+4]"
    // and bounds it against 0x320 (800). Locate each block by its fixed layout.
    struct Block {
        const char* name;
        size_t size;
        size_t markerOffset;
        size_t markerSize;
        BYTE marker[11];
        size_t indexOffset;   // position of "movzx reg, word [ptr+4]"
        BYTE indexBytes[4];   // exact "0F B7 4? 04" form (reg distinguishes block)
    };
    const Block blocks[]={
        {"growth multiplier",40,0x02,4,{0x0F,0xB7,0x46,0x04},0x02,{0x0F,0xB7,0x46,0x04}},
        {"activation boost",56,0x17,6,{0x0F,0xB7,0x5B,0x04,0x81,0xFB},0x17,{0x0F,0xB7,0x5B,0x04}},
        {"critical hit",84,0x1F,4,{0x0F,0xB7,0x40,0x04},0x1F,{0x0F,0xB7,0x40,0x04}},
        {"skill interval",77,0x0F,6,{0x0F,0xB7,0x4E,0x04,0x81,0xF9},0x0F,{0x0F,0xB7,0x4E,0x04}}
    };
    for(const auto& block:blocks) {
        const BYTE* found=nullptr;
        size_t foundCount=0;
        for(const auto& entry:recordedWrites) {
            const auto& bytes=entry.second;
            if(bytes.size()!=block.size) continue;
            if(bytes.size()<block.markerOffset+block.markerSize) continue;
            if(std::memcmp(bytes.data()+block.markerOffset,block.marker,block.markerSize)!=0) continue;
            found=bytes.data();
            ++foundCount;
        }
        check(foundCount==1,"exactly one injected block matches the expected layout");
        check(std::memcmp(found+block.indexOffset,block.indexBytes,4)==0,
            "injected block reads the officer index via word [ptr+4]");
        // B1/B8: the broken derivation was "sub reg, 0x1258EE0" ("2D E0 8E 25 01"
        // for eax, "81 EE E0 8E 25 01" for esi). Reject either inside the payload.
        // The plain immediate "E0 8E 25 01" is fine as a mov-immediate operand and
        // legitimately appears in the game mirror, so only the sub-opcode forms are
        // forbidden here.
        const BYTE subEax[]={0x2D,0xE0,0x8E,0x25,0x01};
        const BYTE subEsi[]={0x81,0xEE,0xE0,0x8E,0x25,0x01};
        for(size_t i=0;i+6<=block.size;++i)
            check(std::memcmp(found+i,subEax,5)!=0 && std::memcmp(found+i,subEsi,6)!=0,
                "injected block does not subtract the stale officer table base 0x1258EE0");
    }
    std::puts("Per-officer injection blocks read the officer index from word [ptr+4].");
}

// 2026-10-31 crash #3 regression.
// The original-tool block unk_42C894 is uploaded to allocationBase+0x9A50 and is
// entered through descriptor 0x42C930, which writes "call 0xA9A50" over the game
// call site 0x51FA13.  The game then does:
//
//   0x51FA13: call 0xA9A50        <- our block
//   0x51FA18: xor edx,edx
//   0x51FA1A: je  0x51FA3B
//   0x51FA3B: mov ecx,eax
//   0x51FA3D: call 0x45C8A0      <- dereferences ecx as "this"
//
// The original block's contract is "return the entry eax unchanged" (its epilogue
// is "mov eax,ebp; ret 4", and ebp holds the entry eax).  A stub that returns 0
// therefore makes the game call 0x45C8A0 with ecx==0 and fault on [ecx+0x28].
// Real-machine WER dump: exception address 0x45C8A0, read address 0x28,
// return address 0x51FA42.  So the disabled stub must NOT clear eax.
void disabledOriginalToolBlockTest() {
    resetBackend();
    Runtime2026 runtime(api());
    version(runtime);
    recordWrites=true;
    initialModify(runtime);
    recordWrites=false;

    // The block is uploaded to allocationBase+0x9A50; find the write that lands
    // there and is short enough to be the disabled stub rather than a full block.
    const std::vector<BYTE>* stub=nullptr;
    for(const auto& entry:recordedWrites) {
        const auto& bytes=entry.second;
        if(bytes.size()<3 || bytes.size()>8) continue;
        if(bytes[0]==0xC2 && bytes[1]==0x04 && bytes[2]==0x00) { stub=&bytes; break; }
    }
    check(stub!=nullptr,"the disabled original-tool block is uploaded as a stub");
    if(stub) {
        check(stub->size()==3,"the disabled stub is exactly \"ret 4\"");
        // "ret 4" keeps the original block's stack effect (one stdcall argument)
        // and leaves eax - the entry value the caller consumes - untouched.
        check((*stub)[0]==0xC2 && (*stub)[1]==0x04 && (*stub)[2]==0x00,
            "the disabled stub returns the entry value instead of clearing eax");
        const BYTE xorEax[]={0x33,0xC0};
        for(size_t i=0;i+2<=stub->size();++i)
            check(std::memcmp(stub->data()+i,xorEax,2)!=0,
                "the disabled stub must not clear eax (crash #3 root cause)");
    }
    std::puts("Disabled original-tool block returns the caller's value instead of zeroing eax.");
}

// 2026-10-31 crash #4 regression.
//
// The game keeps five static tables of ids 0x3E8..0x3F2 in its .data section,
// each entry 0x20 bytes long with the id in entry+0x04:
//
//   0x6613E8  0x665B70  0x667740  0x667C08  0x669F78
//
// The game looks ids up in a manager vector with "cmp dword [entry+4], id" and
// getflag (0x5B72F0) dereferences the lookup result without a null check.  Our
// own applyCultivate() used to write one byte to 0x6613EE, which is byte index 2
// of the id dword at 0x6613EC (value 0x000003E8, id 1000).  The write turned the
// id into 0x000103E8, the lookup for 0x3E8 then failed, and the game faulted at
// 0x5B6754 reading [0+0x14].  No recorded write may touch these tables.
//
// The test also pins the correct implementation of 强制小兵培养: the original
// tool writes the immediate byte of two "cmp eax, 0x32" instructions,
// 0x40CAEF and 0x40BF25, with 0x32 when enabled and 0x00 when disabled.
void idTableWriteGuardTest() {
    // Table base -> eleven entries of 0x20 bytes.
    const DWORD idTables[] = {0x6613E8u, 0x665B70u, 0x667740u, 0x667C08u, 0x669F78u};
    constexpr DWORD idTableEntries = 11;
    constexpr DWORD idTableStride = 0x20;
    // The id dword of table A entry 0 lives at 0x6613EC; 0x6613EE is its byte 2.
    constexpr DWORD idFieldOfTableA = 0x6613ECu;
    constexpr DWORD forbiddenByte = 0x6613EEu;

    auto overlapsIdTable = [&](DWORD destination, size_t size) {
        for (DWORD table : idTables) {
            const DWORD begin = table;
            const DWORD end = table + idTableEntries * idTableStride;
            if (destination < end && begin < destination + size) return true;
        }
        return false;
    };

    resetBackend();
    Runtime2026 runtime(api());
    version(runtime);
    san9::ParameterConfig parameters;
    parameters.set(1013, 1); // 开启小兵培养
    runtime.setParameters(parameters);

    recordWrites=true;
    initialModify(runtime);
    recordWrites=false;

    check(!recordedWrites.empty(),"the runtime recorded its memory writes");
    for (const auto& entry : recordedWrites) {
        const DWORD destination = entry.first;
        const size_t size = entry.second.size();
        check(!overlapsIdTable(destination, size),
            "no recorded write overlaps a static id table region");
        // The former crash #4 write.  Spell it out separately so the failure
        // message names the exact address that used to break id 0x3E8.
        check(!(destination <= forbiddenByte && forbiddenByte < destination + size),
            "the write to 0x6613EE (byte 2 of the id dword 0x6613EC) is gone");
        check(!(destination <= idFieldOfTableA && idFieldOfTableA < destination + size),
            "the id dword 0x6613EC of table A is never written");
    }

    // 强制小兵培养 writes 0x32 into the two "cmp eax, 0x32" immediate bytes.
    auto lastByteWrittenTo = [&](DWORD address) -> int {
        int result = -1;
        for (const auto& entry : recordedWrites)
            if (entry.first == address && entry.second.size() == 1) result = entry.second[0];
        return result;
    };
    check(lastByteWrittenTo(0x40CAEFu) == 0x32,
        "开启小兵培养 writes 0x32 to the immediate byte at 0x40CAEF");
    check(lastByteWrittenTo(0x40BF25u) == 0x32,
        "开启小兵培养 writes 0x32 to the immediate byte at 0x40BF25");

    // Disabled: both immediate bytes go back to 0x00.
    san9::ParameterConfig disabled;
    disabled.set(1013, 0);
    runtime.setParameters(disabled);
    recordWrites=true;
    runtime.modify(ModifyReason2026::ParametersConfirmed);
    recordWrites=false;

    check(lastByteWrittenTo(0x40CAEFu) == 0x00,
        "开启小兵培养 disabled writes 0x00 to the immediate byte at 0x40CAEF");
    check(lastByteWrittenTo(0x40BF25u) == 0x00,
        "开启小兵培养 disabled writes 0x00 to the immediate byte at 0x40BF25");
    for (const auto& entry : recordedWrites) {
        const DWORD destination = entry.first;
        const size_t size = entry.second.size();
        check(!overlapsIdTable(destination, size),
            "no write overlaps a static id table region when the option is switched off");
    }
    std::puts("No runtime write touches the static id tables; 强制小兵培养 targets 0x40CAEF and 0x40BF25.");
}
void lifecycleTest() {
    // Keep the five large runtime fixtures off the unoptimized Debug stack.
    resetBackend();
    {
        auto ownedRuntime = std::make_unique<Runtime2026>(api());
        auto& runtime = *ownedRuntime;
        version(runtime);
        DWORD initialBase=0;RuntimeTestAccess2026{runtime}.dataOut(0x420050,&initialBase,4);
        initialModify(runtime);
        RuntimeTestAccess2026{runtime}.field<DWORD>(120)=0x12345678;RuntimeTestAccess2026{runtime}.field<DWORD>(124)=0x87654321;RuntimeTestAccess2026{runtime}.field<DWORD>(128)=0xC3C3C3C3;
        RuntimeTestAccess2026{runtime}.field<DWORD>(132)=1;RuntimeTestAccess2026{runtime}.field<DWORD>(136)=4;RuntimeTestAccess2026{runtime}.field<DWORD>(140)=9;RuntimeTestAccess2026{runtime}.field<DWORD>(144)=3;
        san9::ParameterConfig config;config.set(1164,1);runtime.setParameters(config);
        const auto written=writes;
        processAlive=false;allocated=false;runtime.modify(ModifyReason2026::TimerTick);
        check(runtime.session().result==OperationResult2026::ProcessExited,"confirmed exit has distinct result");
        check(runtime.session().journal.entries().empty() && !runtime.session().process && closes==1,"exit clears old ownership");
        check(writes==written && releases==0,"exited address space is never restored or freed again");
        DWORD marker=0;RuntimeTestAccess2026{runtime}.dataOut(0x420050,&marker,4);
        check(marker==initialBase && !RuntimeTestAccess2026{runtime}.field<DWORD>(112) && !RuntimeTestAccess2026{runtime}.field<DWORD>(132),"exit restores local address/event baseline");
        check(RuntimeTestAccess2026{runtime}.field<DWORD>(120)==0x12345678 && RuntimeTestAccess2026{runtime}.field<DWORD>(124)==0x87654321
            && RuntimeTestAccess2026{runtime}.field<DWORD>(128)==0xC3C3C3C3,"exit preserves host callback fields");
        for(size_t offset:{108u,112u,116u,132u,136u,140u,144u})
            check(RuntimeTestAccess2026{runtime}.field<DWORD>(offset)==0,"exit resets each recovered lifecycle field");
        // Same HWND, PID and raw handle, but a new process creation time and allocation.
        processAlive=true;processCreated=2;base=0x22000000;memory.clear();protections.clear();
        initialModify(runtime);
        check(writes==written && !allocated && closes==2,"replacement must pass its own version check");
        version(runtime);initialModify(runtime);
        check(runtime.hasGameSession() && runtime.session().identity.created.dwLowDateTime==2,"replacement receives fresh identity");
        check(RuntimeTestAccess2026{runtime}.field<DWORD>(96)==base && load(4733494,2)==59792u,"replacement rebased to new allocation and retains latest options");
        check(runtime.undo() && closes==3 && releases==1,"replacement owns independent resources");
    }
    resetBackend();
    {
        auto ownedRuntime = std::make_unique<Runtime2026>(api());
        auto& runtime = *ownedRuntime;
        version(runtime);initialModify(runtime);
        const auto written=writes;
        exitAfterRead=reads+1;
        check(!runtime.togglePlayerForce(0),"exit between read and write aborts operation");
        check(writes==written && releases==0 && closes==1,"mid-operation exit retires without remote cleanup");
        check(runtime.session().journal.entries().empty() && runtime.session().result==OperationResult2026::ProcessExited,"mid-operation exit clears entire old session");
    }
    resetBackend();
    {
        auto ownedRuntime = std::make_unique<Runtime2026>(api());
        auto& runtime = *ownedRuntime;
        version(runtime);
        fault=Fault::Write;faultAt=30;blockRecovery=true;initialModify(runtime);
        check(runtime.session().phase==SessionPhase2026::RecoveryRequired,"setup pending recovery");
        const auto pending=runtime.session().journal.entries().size();const auto written=writes;
        processAlive=false;allocated=false;fault=Fault::Close;faultAt=calls[static_cast<size_t>(Fault::Close)]+1;fired=false;
        runtime.modify(ModifyReason2026::TimerTick);
        check(runtime.session().journal.entries().size()==pending && closes==0,"failed exited-handle close retains retry ownership");
        check(runtime.status().recoveryFailure.operation==RuntimeOperation2026::CloseExitedProcess
            && runtime.status().recoveryFailure.systemError==ERROR_ACCESS_DENIED
            && runtime.status().failure.operation==RuntimeOperation2026::Write,
            "exit cleanup failure retains the original failure and the separate close error");
        runtime.modify(ModifyReason2026::TimerTick);
        check(runtime.session().journal.entries().empty() && closes==1 && releases==0 && writes==written,"pending recovery can finish by confirmed exit");
        check(!runtime.status().recoveryFailure,"successful exit cleanup clears its recovery error");
        check(runtime.undo() && closes==1,"retired handle is not closed twice");
    }
    resetBackend();
    {
        auto ownedRuntime = std::make_unique<Runtime2026>(api());
        auto& runtime = *ownedRuntime;
        version(runtime);initialModify(runtime);
        const auto written=writes;const auto pending=runtime.session().journal.entries().size();
        identityAvailable=false;runtime.modify(ModifyReason2026::TimerTick);
        check(!runtime.undo() && runtime.session().processState==ProcessState2026::Unavailable,"unavailable identity does not imply exit");
        identityAvailable=true;processCreated=2;
        check(!runtime.undo() && runtime.session().processState==ProcessState2026::IdentityChanged,"reused handle/identity blocks recovery");
        check(writes==written && releases==0 && closes==0 && runtime.session().journal.entries().size()==pending,"unknown or foreign target receives no mutation or close");
        processCreated=1;
        check(runtime.undo(),"original identity allows recovery retry");
    }
    resetBackend();
    {
        auto ownedRuntime = std::make_unique<Runtime2026>(api());
        auto& runtime = *ownedRuntime;
        version(runtime);initialModify(runtime);
        windowHandle=44;runtime.modify(ModifyReason2026::TimerTick);
        check(runtime.session().identity.window==reinterpret_cast<HWND>(44) && closes==0,"same process may recreate its window");
        windowPid=456;runtime.modify(ModifyReason2026::TimerTick);
        check(!runtime.session().process && closes==1 && releases==1,"live old target is undone before switching processes");
        processPid=456;processCreated=2;base=0x22000000;memory.clear();protections.clear();version(runtime);initialModify(runtime);
        check(runtime.hasGameSession() && runtime.session().identity.pid==456,"new window target connects after old cleanup");
        check(runtime.undo(),"handover session cleanup");
    }
    resetBackend();
    std::puts("Five focused lifecycle scenarios passed.");
}
}
void siegeSelectionTest() {
    constexpr DWORD site=0x4B87EB;
    for (bool enabled : {false,true}) {
        resetBackend();
        Runtime2026 runtime(api());version(runtime);
        memory[site]=0x85;memory[site+1]=0xC0;
        runtime.setSkipDurabilitySiegeSelection(enabled);
        initialModify(runtime);
        check(memory[site]==(enabled?0x33:0x85),"siege option applies at startup");
        for (bool next : {!enabled,enabled,!enabled}) {
            runtime.setSkipDurabilitySiegeSelection(next);
            runtime.modify(ModifyReason2026::ParametersConfirmed);
            check(memory[site]==(next?0x33:0x85),"siege option changes live on confirmation");
            check(memory[site+1]==0xC0,"siege option preserves instruction second byte");
        }
        check(runtime.undo(),"siege option undo succeeds");
        check(memory[site]==0x85 && memory[site+1]==0xC0,"siege option undo restores session original");
    }
    std::puts("Focused siege-selection option checks passed; fake backend only.");
}
void computerWorkTimingTest() {
    constexpr DWORD reward=0x6574F0, attack=0x6574C0;
    for (bool improve : {false,true}) for (bool skip : {false,true}) {
        resetBackend();
        Runtime2026 runtime(api()); version(runtime);
        memory[reward]=90; memory[reward+1]=30; memory[reward+2]=1;
        memory[attack]=120; memory[attack+1]=0; memory[attack+2]=255;
        memory[0x657658]=120; // Work code 60 has its own independent delay.
        if (!improve || skip) runtime.setComputerWorkTiming({improve,skip});
        initialModify(runtime);
        check(load(reward,2)==(improve?60u:0x1E5Au) && load(attack,1)==(skip?0u:120u),
              "computer timing defaults and all startup combinations apply");
        for (auto next : {ComputerWorkTiming2026{!improve,!skip}, {improve,skip}}) {
            runtime.setComputerWorkTiming(next);
            runtime.modify(ModifyReason2026::ParametersConfirmed);
            check(load(reward,2)==(next.improveRewards?60u:0x1E5Au) &&
                  load(attack,1)==(next.skipAttackStartDelay?0u:120u),
                  "computer timing options switch independently on live confirmation");
            check(load(reward+2,1)==1 && load(attack+1,2)==0xFF00 && load(0x657658,1)==120,
                  "timing changes preserve permissions, budgets and other work delays");
        }
        check(runtime.undo() && load(reward,2)==0x1E5A && load(attack,1)==120,
              "undo restores both original timing entries");
        version(runtime); initialModify(runtime);
        check(load(reward,2)==(improve?60u:0x1E5Au) && load(attack,1)==(skip?0u:120u),
              "reconnection retains both timing choices");
        check(runtime.undo(),"timing reconnection cleanup succeeds");
    }
    resetBackend();
    Runtime2026 runtime(api()); version(runtime); initialModify(runtime);
    runtime.setComputerWorkTiming({false,true});
    std::function<void()> failAfterReward;
    failAfterReward=[&] {
        if (load(reward,2)==0x1E5A) {
            fault=Fault::Write; faultAt=calls[static_cast<size_t>(Fault::Write)]+1;
        } else writeCallback=failAfterReward;
    };
    writeCallback=failAfterReward;
    runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(fired && runtime.session().result==OperationResult2026::RolledBack &&
          load(reward,2)==60 && load(attack,1)==120,
          "failure between timing writes rolls both choices back to committed state");
    fault=Fault::None;
    runtime.modify(ModifyReason2026::ParametersConfirmed);
    check(load(reward,2)==0x1E5A && load(attack,1)==0,"timing confirmation succeeds after rollback");
    check(runtime.undo(),"timing failure cleanup succeeds");
    resetBackend();
    std::puts("Computer work timing startup, live changes, isolation, rollback and reconnection passed.");
}
void coreBoundaryTest() {
    resetBackend();
    auto owned = std::make_unique<Runtime2026>(api());
    auto& runtime = *owned;
    check(runtime.status().kind == RuntimeStatusKind2026::None, "core starts without presentation state");
    check(!runtime.handleGameMessage(0, 1, 1), "unregistered/offline message ignored");
    const ConfigurationState2026 input{true, true, true, true, true, true};
    runtime.setConfigurationState(input);
    auto state = runtime.configurationState();
    check(state.cityAutomatic && state.cityPending && state.skillLoaded && state.skillPending
        && state.formationLoaded && state.formationPending, "typed configuration flags round-trip");
    runtime.setConfigurationState({});
    const HWND endpoint = reinterpret_cast<HWND>(0x12345678);
    runtime.setCallbackWindow(endpoint);
    check(!runtime.isGameMessage(0xC001), "registered message requires an active session");
    present = false;
    runtime.modify(ModifyReason2026::TimerTick);
    check(runtime.status().kind == RuntimeStatusKind2026::WaitingForGame, "missing game reported without a UI control");
    present = true;
    version(runtime); initialModify(runtime);
    check(load(0x43F7E4) == 0x12345678 && load(0x43F7E8) == 0xC001
        && load(0x43F7EC) == 0xC3C3C3C3, "callback endpoint retains the recovered three-DWORD ABI");
    check(runtime.status().kind == RuntimeStatusKind2026::Connected && runtime.status().errorCount == 0,
        "connection status is structured");
    check(runtime.isGameMessage(0xC001) && !runtime.isGameMessage(0), "only the registered game message accepted");
    auto before = calls;
    check(!runtime.handleGameMessage(0xC002, 1, 1) && calls == before, "unrelated message performs no process operations");
    runtime.handleGameMessage(0xC001, 1, 0);
    check(calls == before, "empty game notification ignored");
    runtime.handleGameMessage(0xC001, 2, 1020);
    runtime.handleGameMessage(0xC001, 1, 1);
    runtime.handleGameMessage(0xC001, 2, 1003);
    runtime.handleGameMessage(0xC001, 2, 0);
    check(calls == before, "cancelled dialog sequence performs no modification");
    runtime.setConfigurationState({true, false, false, false, false, false});
    runtime.handleGameMessage(0xC001, 2, 1020);
    runtime.handleGameMessage(0xC001, 2, 0);
    check(writes > before[static_cast<size_t>(Fault::Write)] && runtime.session().result == OperationResult2026::Committed,
        "completed dialog dispatches its configuration transaction");
    before = calls;
    runtime.handleGameMessage(0xC001, 1, 1);
    check(calls[static_cast<size_t>(Fault::Write)] > before[static_cast<size_t>(Fault::Write)],
        "game notification dispatches its configuration transaction");
    const auto committed = memory;
    const auto committedProtections = protections;
    fault = Fault::Write; faultAt = calls[static_cast<size_t>(Fault::Write)] + 1;
    partialFailure = true; shortSuccess = false;
    runtime.handleGameMessage(0xC001, 1, 1);
    check(fired && runtime.session().result == OperationResult2026::RolledBack
        && runtime.status().kind == RuntimeStatusKind2026::ModificationRolledBack,
        "message-triggered failure returns rollback status without GUI code");
    check(protections == committedProtections && runtime.hasGameSession() && allocated
        && releases == 0 && closes == 0, "message rollback retains the committed session and protection state");
    for (const auto& pair : memory) {
        const auto found = committed.find(pair.first);
        check(pair.second == (found == committed.end() ? 0 : found->second), "message rollback restores committed bytes");
    }
    check(runtime.undo() && runtime.status().kind == RuntimeStatusKind2026::Restored,
        "headless caller can undo and observe completion");
    check(!runtime.isGameMessage(0xC001), "game messages ignored after undo");
    resetBackend();
    std::puts("Core typed state, callback ABI, message dispatch and headless status checks passed.");
}

void gameSnapshotsTest() {
    resetBackend();
    auto owned = std::make_unique<Runtime2026>(api()); auto& runtime=*owned;
    SkillSnapshot2026 skill; skill.names[0][0]='X';
    check(!runtime.readSkillSnapshot(skill) && skill.names[0][0]=='X', "offline snapshot leaves output untouched");
    version(runtime); initialModify(runtime); runtime.setConfigurationState({});
    const auto writeCount=writes;
    for(DWORD i=0;i<san9::SkillConfig::Count;++i) {
        const DWORD row=0x14FDAD0+i*0x8C; memory[row+0x64]=17;memory[row+0x7C]=6;
        store(row+0x58,1234);memory[row+0x10]='S';memory[row+0x11]='K';memory[row+0x12]=0;
    }
    const auto savedSkill=runtime.skillConfig().encode();
    check(runtime.readSkillSnapshot(skill) && !skill.invalidRow && skill.config.entries.back().power==17
        && skill.config.entries.back().interval==6 && skill.config.entries.back().proficiency==1234
        && std::strcmp(skill.names[0].data(),"SK")==0, "skill snapshot decodes fields and names");
    check(runtime.skillConfig().encode()==savedSkill, "snapshot import remains an editor draft");
    memory[0x14FDAD0+38*0x8C+0x64]=255;
    check(runtime.readSkillSnapshot(skill) && skill.invalidRow==38 && skill.config.encode()==savedSkill,
        "invalid final skill row discards all preceding imported rows");
    runtime.setConfigurationState({false,false,true,false,false,false});
    check(runtime.readSkillSnapshot(skill) && !skill.invalidRow && skill.config.encode()==savedSkill,
        "file-loaded skill settings suppress game import");
    runtime.setConfigurationState({});
    san9::FormationConfig savedFormation;for(auto& row:savedFormation.entries)row.range=4;runtime.setFormationConfig(savedFormation);
    for(DWORD i=0;i<san9::FormationConfig::Count;++i){const DWORD row=0x1500B70+i*0x88;
        for(DWORD offset:{0x60u,0x64u,0x68u,0x6Cu,0x5Cu})memory[row+offset]=12;
        memory[row+0x70]=2;memory[row+0x74]=3;store(row+0x78,500);memory[row+0x10]='F';memory[row+0x11]=0;}
    FormationSnapshot2026 formation;
    check(runtime.readFormationSnapshot(formation)&&!formation.invalidRow&&formation.config.entries.back().troop==12
        &&formation.config.entries.back().expense==500&&formation.config.entries.back().range==4,"formation snapshot retains companion range");
    memory[0x1500B70+26*0x88+0x60]=61;
    check(runtime.readFormationSnapshot(formation)&&formation.invalidRow==26&&formation.config.encode()==savedFormation.encode(),"formation import rejects entire invalid draft");
    runtime.setConfigurationState({false,false,false,false,false,true});
    check(runtime.readFormationSnapshot(formation)&&!formation.invalidRow&&formation.config.encode()==savedFormation.encode(),"pending formation settings suppress import");
    runtime.setConfigurationState({});
    for(DWORD i=0;i<san9::CityConfig::Count;++i){const DWORD row=0x124DB58+i*0x1F0;store(row+0x1C8,700);store(row+0x1D4,900);store(row+0x1D8,1100);memory[row+0x20]='C';memory[row+0x21]=0;}
    const auto savedCity=runtime.cityConfig(); CitySnapshot2026 city;
    check(runtime.readCitySnapshot(city)&&!city.invalidRow&&city.config.limits.back().income==9
        &&city.config.limits.back().harvest==11&&city.config.relations==savedCity.relations,"city snapshot imports only editable game fields");
    store(0x124DB58+49*0x1F0+0x1D4,999999);
    check(runtime.readCitySnapshot(city)&&city.invalidRow==49&&city.config.encodeLimits()==savedCity.encodeLimits(),"city import is atomic");
    for(auto flags:{ConfigurationState2026{true,false,false,false,false,false},ConfigurationState2026{false,true,false,false,false,false}}){runtime.setConfigurationState(flags);check(runtime.readCitySnapshot(city)&&!city.invalidRow&&city.config.encodeLimits()==savedCity.encodeLimits(),"automatic or pending city settings suppress import");}
    san9::PersonSnapshot persons;
    const DWORD personBase=0x1258EE0;
    memory[personBase+0x10]='A';memory[personBase+0x11]='B';memory[personBase+0x12]=0;
    memory[personBase+0x15]='C';memory[personBase+0x16]='D';memory[personBase+0x17]=0;
    memory[personBase+0x84]=255;memory[personBase+0x64]=98;
    check(runtime.readPersonSnapshot(persons)&&persons.size()==800&&std::strcmp(persons.at(0).name.data(),"ABCD")==0
        &&persons.at(0).status==10&&persons.at(0).abilities[0]==98,"person bulk snapshot uses bounded model decoding");
    const DWORD forceBase=0x1253C38;
    for(DWORD i=0;i<50*0xD4;++i)memory[forceBase+i]=0;
    auto setForce=[&](unsigned index,BYTE flag,DWORD owner,DWORD leader){const DWORD row=forceBase+index*0xD4;memory[row+0x34]=flag;store(row+0xB8,owner);store(row+0xBC,leader);};
    setForce(0,1,forceBase,personBase);setForce(1,4,forceBase+0xD4,personBase);
    setForce(2,0,forceBase+2*0xD4,personBase+0x128);setForce(3,0,forceBase,personBase);
    setForce(4,0,forceBase+4*0xD4,0);memory[personBase+0x128+0x10]='E';memory[personBase+0x128+0x11]=0;
    std::vector<std::pair<DWORD,SIZE_T>> requests; recordedReads=&requests;
    std::vector<PlayerForce2026> forces;
    check(runtime.readPlayerForces(forces)&&forces.size()==2&&forces[0].index==0&&forces[0].player
        &&forces[1].index==2&&!forces[1].player&&std::strcmp(forces[0].leaderName.data(),"ABCD")==0,"force snapshot filters barbarians, subordinate and leaderless forces");
    recordedReads=nullptr;
    const std::vector<std::pair<DWORD,SIZE_T>> expected{{forceBase,50*0xD4},{personBase,0x128},{personBase+0x128,0x128}};
    check(requests==expected,"force snapshot preserves original bulk/leader read order and widths");
    fault=Fault::Read;faultAt=calls[static_cast<size_t>(Fault::Read)]+3;partialFailure=true;
    check(!runtime.readPlayerForces(forces)&&fired&&forces.size()==2&&forces[0].index==0,"partial force read never publishes an incomplete list");
    check(writes==writeCount,"all game snapshots perform no remote writes");
    fault=Fault::None;fired=false;processAlive=false;allocated=false;
    check(!runtime.readSkillSnapshot(skill)&&runtime.session().result==OperationResult2026::ProcessExited
        &&runtime.session().journal.entries().empty(),"snapshot reads retire exited sessions through the common lifecycle");
    check(runtime.undo(),"snapshot exit cleanup remains idempotent");resetBackend();
    std::puts("Typed game snapshots, import flags, atomic invalid rows, force read order and exit checks passed.");
}

void controllerTest(const std::filesystem::path &configurationDirectory) {
    using app::ConfigurationSource;
    resetBackend();
    app::Controller controller(api());
    auto& runtime = app::ControllerTestAccess::runtime(controller);
    auto skills = controller.configuration().skills;
    skills.entries[0].power = 91;
    check(controller.configuration().skills.entries[0].power != 91, "editing a value draft does not submit it");
    check(controller.commitSkills(skills), "disconnected skill confirmation accepted");
    auto invalid = skills; invalid.entries[0].power = 101;
    check(!controller.commitSkills(invalid) && controller.configuration().skills.entries[0].power == 91,
          "invalid confirmation leaves desired state unchanged");
    present = false;
    controller.poll();
    check(writes == 0 && controller.configuration().skills.entries[0].power == 91,
          "waiting for a game preserves confirmed child configuration");
    present = true; version(runtime);
    for (int i=0; i<6; ++i) controller.poll();
    check(controller.hasGameSession() && load(0x14FDAD0+0x64)==91, "initial connection applies disconnected edits");
    check(!controller.state().runtimeConfiguration.skillPending, "core owns consumption of pending state");
    auto effective = runtime.skillConfig(); effective.entries[0].power = 70;
    runtime.setSkillConfig(effective);
    memory[0x14FDAD0+0x64] = 255;
    SkillSnapshot2026 snapshot;
    const auto snapshotWrites = writes;
    check(controller.skillSnapshot(snapshot) && snapshot.invalidRow == 0 && snapshot.config.entries[0].power == 70,
          "invalid game import falls back to the effective core configuration");
    check(controller.configuration().skills.entries[0].power == 91 && runtime.skillConfig().entries[0].power == 70 &&
          writes == snapshotWrites, "snapshot neither stages desired values nor overwrites them");
    const auto desiredCitySoldiers = controller.configuration().cities.limits[0].soldiers;
    auto effectiveCity = runtime.cityConfig(); effectiveCity.limits[0].soldiers = 17;
    runtime.setCityConfig(effectiveCity);
    CitySnapshot2026 citySnapshot;
    check(controller.citySnapshot(citySnapshot) && citySnapshot.invalidRow && citySnapshot.config.limits[0].soldiers==17 &&
          controller.configuration().cities.limits[0].soldiers==desiredCitySoldiers,
          "derived default fallback is a snapshot view, not a replacement for desired city input");
    check(controller.undo() && controller.state().inputsQueued && controller.configuration().skills.entries[0].power == 91,
          "undo retains desired inputs and rearms the next session");
    version(runtime);
    for (int i=0; i<6; ++i) controller.poll();
    check(controller.hasGameSession() && load(0x14FDAD0+0x64)==91, "reconnect restores the confirmed input, not derived state");
    auto retriedParameters = controller.configuration().parameters; retriedParameters.set(1164,1);
    check(controller.commitParameters(retriedParameters,true,{},{},{},{}), "parameter confirmation before recoverable failure");
    fault=Fault::Write; faultAt=calls[static_cast<size_t>(Fault::Write)]+3; partialFailure=true;
    controller.applyConfirmedConfiguration();
    check(controller.state().result==OperationResult2026::RolledBack && controller.state().confirmationPending &&
          controller.configuration().parameters.get(1164)==1, "successful rollback does not acknowledge unapplied parameters");
    controller.poll();
    check(!controller.state().confirmationPending && load(4733494,2)==59792,
          "next poll retries a failed parameter confirmation instead of reporting idle success");
    auto inFlight = controller.configuration().parameters; inFlight.set(1164,0);
    check(controller.commitParameters(inFlight,true,{},{},{},{}), "submit first in-flight parameter revision");
    writeCallback = [&] {
        check(controller.state().phase==SessionPhase2026::Applying, "confirmation callback runs during application");
        inFlight.set(1164,1);
        check(controller.commitParameters(inFlight,true,{},{},{},{}), "new revision accepted during Applying");
    };
    controller.applyConfirmedConfiguration();
    check(controller.state().inputsQueued && controller.state().confirmationPending && load(4733494,2)==33807,
          "committing the older operation cannot acknowledge the newer queued confirmation");
    controller.poll();
    check(!controller.state().inputsQueued && !controller.state().confirmationPending && load(4733494,2)==59792,
          "next poll stages and applies the in-flight confirmation");
    check(controller.undo(), "close first controller scenario");

    resetBackend();
    app::Controller recovery(api());
    auto& core = app::ControllerTestAccess::runtime(recovery);
    check(recovery.load(configurationDirectory, true, true,{}).empty(), "controller loads all configuration fixtures");
    check(recovery.configuration().sources.skills == ConfigurationSource::File, "file origin is explicit");
    version(core);
    for (int i=0; i<6; ++i) recovery.poll();
    check(recovery.hasGameSession() && !recovery.state().confirmationPending, "successful connection acknowledges parameters");
    auto parameters = recovery.configuration().parameters; parameters.set(1164,1);
    check(recovery.commitParameters(parameters,true,{},{},{},{}), "first parameter confirmation");
    fault=Fault::Write; faultAt=calls[static_cast<size_t>(Fault::Write)]+15;
    partialFailure=true; blockRecovery=true;
    recovery.applyConfirmedConfiguration();
    check(fired && recovery.state().phase==SessionPhase2026::RecoveryRequired && recovery.state().confirmationPending,
          "failed live application remains pending until recovery and commit");
    const auto retainedSkill = core.skillConfig().encode();
    const auto retainedPerson = core.personConfig().encodeLevels();
    const auto retainedCity = core.cityConfig().encodeLimits();
    const auto retainedFormation = core.formationConfig().encode();
    const auto pendingChanges = core.session().journal.entries().size();
    parameters.set(1164,0);
    auto latestSkills = recovery.configuration().skills; latestSkills.entries[0].power = 77;
    auto latestPersons = recovery.configuration().persons; latestPersons.levels[0] = 1;
    auto latestCities = recovery.configuration().cities; latestCities.limits[0].income = 11;
    auto latestFormations = recovery.configuration().formations; latestFormations.entries[0].range = 4;
    check(recovery.commitParameters(parameters,false,{false,true},{},{},{}) && recovery.commitSkills(latestSkills) &&
          recovery.commitPersons(latestPersons) && recovery.commitCities(latestCities,true) &&
          recovery.commitFormations(latestFormations), "all five configuration groups can queue during recovery");
    check(core.skillConfig().encode()==retainedSkill && core.personConfig().encodeLevels()==retainedPerson &&
          core.cityConfig().encodeLimits()==retainedCity && core.formationConfig().encode()==retainedFormation &&
          core.session().journal.entries().size()==pendingChanges && recovery.state().inputsQueued,
          "queued edits cannot touch the failed host projection or journal");
    const auto priorWrites = writes;
    recovery.poll();
    check(!recovery.skillSnapshot(snapshot) && writes==priorWrites && recovery.configuration().skills.entries[0].power==77,
          "blocked polling and snapshot reads retain newer confirmations without writes");
    check(!recovery.undo() && recovery.configuration().persons.levels[0]==1,
          "failed close retains both recovery and latest desired inputs");
    blockRecovery=false; fault=Fault::None;
    check(recovery.retryRecovery() && !recovery.state().inputsQueued && core.skillConfig().entries[0].power==77,
          "successful recovery stages queued changes only after rollback");
    check(recovery.configuration().sources.skills==ConfigurationSource::ConfirmedFile &&
          recovery.state().runtimeConfiguration.skillLoaded && recovery.state().runtimeConfiguration.skillPending,
          "confirming file-backed edits retains continuous policy and schedules application");
    check(core.cityConfig().limits[0].income==11 && core.formationConfig().entries[0].range==4 &&
          recovery.state().runtimeConfiguration.cityPending && recovery.state().runtimeConfiguration.formationPending,
          "recovery stages latest city and formation confirmations with pending state");
    recovery.poll();
    check(recovery.hasGameSession() && !recovery.state().confirmationPending && load(4733494,2)==33807 &&
          load(0x4B87EB,1)==0x85 && load(0x14FDAD0+0x64)==77 &&
          load(0x6574F0,2)==0x1E5A && load(0x6574C0,1)==0,
          "post-recovery retry commits the newest parameters, extension and skill values");
    const auto rangeAddress = RuntimeTestAccess2026{core}.field<DWORD>(san9::injection2026::FormationAttackRangeAddress);
    check(load(0x124DBAA+386)==1100 && load(rangeAddress,1)==4 &&
          !recovery.state().runtimeConfiguration.cityPending && !recovery.state().runtimeConfiguration.formationPending,
          "successful retry applies city and formation changes and consumes their pending state");
    const auto message = runtime.isGameMessage(0xC001); // Earlier closed session must not leak its callback.
    check(!message && recovery.gameMessage(0xC001,1,1), "game messages route only through the active controller");
    check(recovery.configuration().persons.levels[0]==1 && recovery.configuration().skills.entries[0].power==77,
          "game notification cannot replace desired input with effective state");
    const auto epoch = core.configurationEpoch();
    processAlive=false; allocated=false;
    check(!recovery.skillSnapshot(snapshot) && core.configurationEpoch()!=epoch && recovery.state().inputsQueued,
          "read-triggered process exit queues desired state for reconnection");
    check(recovery.configuration().skills.entries[0].power==77 && recovery.configuration().persons.levels[0]==1,
          "exit cleanup retains confirmations from the recovery interval");
    processAlive=true; ++processCreated; version(core);
    for (int i=0; i<6; ++i) recovery.poll();
    check(recovery.hasGameSession() && load(0x14FDAD0+0x64)==77 && load(0x4B87EB,1)==0x85 &&
          recovery.configuration().sources.skills==ConfigurationSource::ConfirmedFile &&
          load(0x6574F0,2)==0x1E5A && load(0x6574C0,1)==0,
          "new process receives the newest inputs with their original application policy");
    const auto reconnectedRange = RuntimeTestAccess2026{core}.field<DWORD>(san9::injection2026::FormationAttackRangeAddress);
    check(load(0x124DBAA+386)==1100 && load(reconnectedRange,1)==4,
          "new process reapplies queued city and formation confirmations");
    const auto sources = recovery.configuration().sources.skills;
    check(recovery.load(configurationDirectory / "absent", true, false,{}).size()==5 &&
          recovery.configuration().sources.skills==sources && recovery.configuration().skills.entries[0].power==77,
          "failed reload preserves configuration and source policy");
    check(recovery.undo(), "final controller session restores successfully");
    resetBackend();
    std::puts("Controller drafts, typed confirmations, snapshots, recovery queues, file policies and reconnection passed.");
}

void presentationSpeedRuntimeTest() {
    auto seed=[] {
        store(0x5FF4D8,0x70001000);
        for (const auto& hook:presentation2026::hooks())
            for (size_t i=0;i<hook.original.size();++i) memory[hook.site+DWORD(i)]=hook.original[i];
    };
    auto connected=[&](Runtime2026& runtime) { version(runtime); initialModify(runtime); seed(); };
    auto clean=[] { check(barrierSuspended==0,"all acquired suspension counts are released"); };
    resetBackend();
    {
        auto owner=std::make_unique<Runtime2026>(api()); auto& runtime=*owner; connected(runtime);
        const auto before=memory;
        runtime.setPresentationSpeed(2); runtime.modify(ModifyReason2026::PresentationSpeed);
        check(RuntimeTestAccess2026{runtime}.appliedPresentationSpeed()==2 && speedAllocated,"speed installation commits"); clean();
        for (const auto& hook:presentation2026::hooks()) {
            auto patch=presentation2026::redirect(hook,base+0x10000);
            for (size_t i=0;i<patch.size();++i) check(memory[hook.site+DWORD(i)]==patch[i],"all five owned detours installed");
        }
        const auto journalSize=runtime.session().journal.size();
        for (unsigned i=0;i<12;++i) {
            runtime.setPresentationSpeed(i%2?4:1); runtime.modify(ModifyReason2026::PresentationSpeed);
            check(RuntimeTestAccess2026{runtime}.appliedPresentationSpeed()==(i%2?4u:1u),"live switch commits requested multiplier"); clean();
        }
        check(runtime.session().journal.size()==journalSize,"switching does not grow journal history");
        const auto writeCount=writes, readCount=reads;
        runtime.modify(ModifyReason2026::TimerTick);
        check(writes==writeCount && reads==readCount && barrierSuspended==0,"idle speed polling performs no remote changes");
        check(runtime.undo() && !speedAllocated && !allocated,"undo frees both allocations"); clean();
        for (const auto& hook:presentation2026::hooks())
            for (size_t i=0;i<hook.original.size();++i) check(memory[hook.site+DWORD(i)]==before.at(hook.site+DWORD(i)),"undo restores original instruction bytes");
        check(load(0x5FF4D8)==0x70001000,"clock import was never modified");
    }
    // Partial installation, partial writes, rollback failures and retry must keep executable memory owned.
    for (unsigned point=1;point<=6;++point) {
        resetBackend(); auto owner=std::make_unique<Runtime2026>(api()); auto& runtime=*owner; connected(runtime);
        fault=Fault::Write; faultAt=calls[static_cast<size_t>(Fault::Write)]+point;
        partialFailure=true; shortSuccess=false;
        runtime.setPresentationSpeed(2); runtime.modify(ModifyReason2026::PresentationSpeed);
        check(fired && runtime.session().result==OperationResult2026::RolledBack && !speedAllocated,
              "failed speed installation rolls back payload and every partial detour"); clean();
        check(RuntimeTestAccess2026{runtime}.appliedPresentationSpeed()==1,"failed installation preserves applied multiplier");
        fault=Fault::None;
        runtime.modify(ModifyReason2026::TimerTick);
        check(RuntimeTestAccess2026{runtime}.appliedPresentationSpeed()==2 && speedAllocated,"poll retries desired speed after rollback");
        check(runtime.undo(),"recovered speed installation restores"); clean();
    }
    for (auto kind:{BarrierFault::Snapshot,BarrierFault::Open,BarrierFault::Suspend,BarrierFault::Context}) {
        resetBackend(); auto owner=std::make_unique<Runtime2026>(api()); auto& runtime=*owner; connected(runtime);
        barrierFault=kind;
        runtime.setPresentationSpeed(2); runtime.modify(ModifyReason2026::PresentationSpeed);
        check(RuntimeTestAccess2026{runtime}.appliedPresentationSpeed()==1 && !speedAllocated,"failed thread barrier prevents speed writes"); clean();
        barrierFault=BarrierFault::None;
        runtime.modify(ModifyReason2026::TimerTick);
        check(RuntimeTestAccess2026{runtime}.appliedPresentationSpeed()==2,"barrier failure retries");
        check(runtime.undo(),"barrier retry closes"); clean();
    }
    resetBackend();
    {
        auto owner=std::make_unique<Runtime2026>(api()); auto& runtime=*owner; connected(runtime);
        barrierEip=presentation2026::hooks()[0].site+1;
        runtime.setPresentationSpeed(2); runtime.modify(ModifyReason2026::PresentationSpeed);
        check(runtime.status().failure.operation==RuntimeOperation2026::SpeedBusy && !speedAllocated,
              "instruction boundary conflict prevents install"); clean();
        barrierEip=0;
        runtime.modify(ModifyReason2026::TimerTick);
        barrierEip=base+0x10040;
        check(!runtime.undo() && speedAllocated,"active payload prevents release during undo"); clean();
        barrierEip=0;
        check(runtime.undo() && !speedAllocated,"busy undo retries safely"); clean();
    }
    resetBackend();
    {
        auto owner=std::make_unique<Runtime2026>(api()); auto& runtime=*owner; connected(runtime);
        runtime.setPresentationSpeed(2); runtime.modify(ModifyReason2026::PresentationSpeed);
        runtime.setPresentationSpeed(4);
        barrierFault=BarrierFault::Resume;
        runtime.modify(ModifyReason2026::PresentationSpeed);
        check(runtime.session().phase==SessionPhase2026::RecoveryRequired && barrierSuspended==1,
              "resume failure preserves suspended thread ownership and blocks operations");
        check(!runtime.retryRollback(),"persistent resume failure cannot discard recovery");
        barrierFault=BarrierFault::None;
        check(runtime.retryRollback() && RuntimeTestAccess2026{runtime}.appliedPresentationSpeed()==2,"resume retry rolls back incomplete live switch"); clean();
        check(runtime.undo(),"resume failure ultimately restores"); clean();
    }
    resetBackend();
    {
        auto owner=std::make_unique<Runtime2026>(api()); auto& runtime=*owner; connected(runtime);
        store(0x5FF4D8,0x71001000);
        runtime.setPresentationSpeed(2); runtime.modify(ModifyReason2026::PresentationSpeed);
        check(!speedAllocated && runtime.status().failure.operation==RuntimeOperation2026::ValidateSpeed,
              "toolkit or other external clock hook is rejected");
        store(0x5FF4D8,0x70001000);
        memory[presentation2026::hooks()[0].site]=0xE9;
        runtime.modify(ModifyReason2026::TimerTick);
        check(!speedAllocated && runtime.status().failure.operation==RuntimeOperation2026::ValidateSpeed,
              "foreign instruction detour is rejected");
        check(runtime.undo(),"foreign entry rejection leaves ordinary session recoverable"); clean();
    }
    resetBackend();
    presentationSpeedMachineCodeTest();
    std::puts("Presentation session: installation, live switching, rollback, thread barriers, foreign ownership and undo passed.");
}

void configFilesTest();
int main(int argc, char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    _set_invalid_parameter_handler([](const wchar_t* expression,const wchar_t* function,const wchar_t* file,unsigned line,uintptr_t) {
        std::fwprintf(stderr,L"CRT invalid parameter: %ls in %ls (%ls:%u)\n",expression?expression:L"?",function?function:L"?",file?file:L"?",line);
        std::exit(2);
    });
    try {
        // Record actual addresses, lengths and contents for before/after refactoring.
        // Native source pointers and filenames are deliberately absent.
        if(argc==4 && std::strcmp(argv[1],"--memory-trace")==0) {
            memoryTrace.open(argv[2],std::ios::binary|std::ios::trunc);
            if (!memoryTrace) throw std::runtime_error("cannot open memory trace");
            resetBackend();
            branchTest(false);branchTest(true);branchTest(false,true);branchTest(true,true);
            unsupportedVersionTest();siegeSelectionTest();
            const auto root=std::filesystem::absolute(argv[3]);
            personConfigTest((root/"San9PKHardPerson.txt").string().c_str());
            formationConfigTest((root/"San9PKHardArray.txt").string().c_str());
            skillConfigTest((root/"San9PKHardSkill.txt").string().c_str());
            cityConfigTest((root/"San9PKHardCity.txt").string().c_str());
            memoryTrace.close();
            return 0;
        }
        const std::pair<const char*,void(*)()> focused[] = {
            {"--presentation-speed",presentationSpeedRuntimeTest},
            {"--config-files",configFilesTest},
            {"--journal",journalTest},{"--diagnostics",diagnosticsTest},{"--app-paths",appPathsTest},{"--core-boundary",coreBoundaryTest},{"--game-snapshots",gameSnapshotsTest},
            {"--siege-selection",siegeSelectionTest},{"--state-bounds",stateBoundsTest},{"--image-bounds",imageBoundsTest},
            {"--computer-work-timing",computerWorkTimingTest},
            {"--game-text",gameTextTest},{"--person-snapshot",personSnapshotTest},
            {"--lifecycle",lifecycleTest},{"--personalization",personalizationConfigTest},
            {"--injection-code-bytes",injectionCodeBytesTest},
            {"--disabled-original-block",disabledOriginalToolBlockTest},
            {"--id-table-write-guard",idTableWriteGuardTest}
        };
        const struct ConfigCheck {
            const char* option;
            const char* file;
            void(*run)(const char*);
        } configurations[] = {
            {"--person","San9PKHardPerson.txt",personConfigTest},
            {"--formation","San9PKHardArray.txt",formationConfigTest},
            {"--skill","San9PKHardSkill.txt",skillConfigTest},
            {"--city","San9PKHardCity.txt",cityConfigTest}
        };
        for(const auto& entry:focused)
            if(argc==2 && std::strcmp(argv[1],entry.first)==0) { entry.second();return 0; }
        for(const auto& entry:configurations)
            if(argc==3 && std::strcmp(argv[1],entry.option)==0) { entry.run(argv[2]);return 0; }
        if(argc==2 && std::strcmp(argv[1],"--dispatch")==0) {
            branchTest(false);branchTest(true);unsupportedVersionTest();dispatchTest();return 0;
        }
        if(argc==3 && std::strcmp(argv[1],"--controller")==0) { controllerTest(std::filesystem::absolute(argv[2])); return 0; }
        const bool all=argc==3 && std::strcmp(argv[1],"--all")==0;
        const bool quick=argc==3 && std::strcmp(argv[1],"--quick")==0;
        const bool configurationsRequested=all || quick;
        if(argc!=1 && !configurationsRequested) {
            std::fputs("Unknown arguments; use --all/--quick <configuration directory> or a supported focused check.\n",stderr);
            return 2;
        }
        const auto configRoot=configurationsRequested?std::filesystem::absolute(argv[2]):std::filesystem::path{};
        if(configurationsRequested) for(const auto& entry:configurations)
            if(!std::filesystem::is_regular_file(configRoot/entry.file))
                throw std::runtime_error("missing configuration fixture: "+(configRoot/entry.file).string());
        resetBackend();
        std::puts("Checking runtime relocation...");rebaseTest();
        std::puts("Checking runtime branches...");
        branchTest(false);branchTest(true);unsupportedVersionTest();
        branchTest(false,true);branchTest(true,true);dispatchTest();
        std::puts("Checking transaction failures...");
        recoveryTest();liveFailureTest();
        protectionRegionTest(false);protectionRegionTest(true);windowChangeTest();
        playerForceTest();
        for(auto kind:{Fault::Write,Fault::Flush,Fault::Protect,Fault::Release,Fault::Close}) undoFailureTest(kind);
        if (!quick) faultSweep();
        lifecycleTest();
        journalTest();
        diagnosticsTest();
        if(configurationsRequested) {
            presentationSpeedRuntimeTest();
            configFilesTest(); controllerTest(configRoot);
            coreBoundaryTest();gameSnapshotsTest();appPathsTest();stateBoundsTest();imageBoundsTest();gameTextTest();personSnapshotTest();siegeSelectionTest();computerWorkTimingTest();personalizationConfigTest();injectionCodeBytesTest();disabledOriginalToolBlockTest();idTableWriteGuardTest();
            for(const auto& entry:configurations) entry.run((configRoot/entry.file).string().c_str());
        }
    } catch(const std::exception& error) {
        std::fprintf(stderr,"runtime exception at write %zu: %s\n",writes,error.what());
        return 3;
    }
    std::puts("2026 offline runtime checks passed; no game process or GUI was accessed.");
}
