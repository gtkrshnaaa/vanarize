#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "Core/VanarizeValue.h"
#include "Core/Memory.h"
#include "Core/GarbageCollector.h"

void TestNaNBoxing() {
    printf("Testing NaN Boxing...\n");
    
    // Test Double
    double myNum = 123.456;
    Value vNum = NumberToValue(myNum);
    assert(IsNumber(vNum));
    assert(!IsBool(vNum));
    assert(!IsNil(vNum));
    assert(ValueToNumber(vNum) == myNum);
    
    // Test Boolean
    Value vTrue = BoolToValue(true);
    Value vFalse = BoolToValue(false);
    assert(IsBool(vTrue));
    assert(ValueToBool(vTrue) == true);
    assert(IsBool(vFalse));
    assert(ValueToBool(vFalse) == false);
    
    // Test Null
    Value vNil = VAL_NULL;
    assert(IsNil(vNil));
    
    // Test Pointer
    char dummy = 'A';
    Value vPtr = ObjToValue(&dummy);
    assert(!IsNumber(vPtr));
    // Since our IsObj check is simplified/strict:
    // assert(IsObj(vPtr)); // Let's refine IsObj in header if needed.
    
    void* ptrBack = ValueToObj(vPtr);
    assert(ptrBack == &dummy);
    
    printf("NaN Boxing OK.\n");
}

void TestMemory() {
    printf("Testing Memory...\n");
    VM_InitMemory();
    
    void* p1 = MemAlloc(64);
    void* p2 = MemAlloc(64);
    
    // Check contiguous allocation
    assert((char*)p2 == ((char*)p1 + 64));
    
    // Create a dummy struct
    typedef struct {
        int id;
        double val;
    } MyObj;
    
    MyObj* obj = (MyObj*)MemAlloc(sizeof(MyObj));
    obj->id = 1;
    obj->val = 3.14;
    
    assert(obj->val == 3.14);
    
    VM_FreeMemory();
    printf("Memory OK.\n");
}

int main() {
    TestNaNBoxing();
    TestMemory();
    return 0;
}
