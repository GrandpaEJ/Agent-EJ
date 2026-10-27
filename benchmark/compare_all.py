import struct
import sys
import zipfile

def parse_uleb128(data, off):
    result = 0
    shift = 0
    while True:
        b = data[off]
        off += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    return result, off

def read_all_methods(dex_path):
    with open(dex_path, 'rb') as f:
        data = f.read()
    
    string_ids_size, string_ids_off = struct.unpack('<II', data[56:64])
    type_ids_size, type_ids_off = struct.unpack('<II', data[64:72])
    proto_ids_size, proto_ids_off = struct.unpack('<II', data[72:80])
    field_ids_size, field_ids_off = struct.unpack('<II', data[80:88])
    method_ids_size, method_ids_off = struct.unpack('<II', data[88:96])
    class_defs_size, class_defs_off = struct.unpack('<II', data[96:104])
    
    def get_string(idx):
        if idx >= string_ids_size: return None
        off = struct.unpack('<I', data[string_ids_off + idx * 4:string_ids_off + idx * 4 + 4])[0]
        length, off = parse_uleb128(data, off)
        return data[off:off+length].decode('utf-8', errors='ignore')
    
    def get_type(idx):
        if idx >= type_ids_size: return None
        str_idx = struct.unpack('<I', data[type_ids_off + idx * 4:type_ids_off + idx * 4 + 4])[0]
        return get_string(str_idx)
        
    def get_method_name_and_class(idx):
        class_idx, proto_idx, name_idx = struct.unpack('<HHI', data[method_ids_off + idx * 8:method_ids_off + idx * 8 + 8])
        return get_type(class_idx), get_string(name_idx)

    methods = {}
    for i in range(class_defs_size):
        off = class_defs_off + i * 32
        class_idx = struct.unpack('<I', data[off:off+4])[0]
        desc = get_type(class_idx)
        class_data_off = struct.unpack('<I', data[off+24:off+28])[0]
        if class_data_off == 0: continue
        
        coff = class_data_off
        static_fields_size, coff = parse_uleb128(data, coff)
        instance_fields_size, coff = parse_uleb128(data, coff)
        direct_methods_size, coff = parse_uleb128(data, coff)
        virtual_methods_size, coff = parse_uleb128(data, coff)
        
        for _ in range(static_fields_size):
            _, coff = parse_uleb128(data, coff)
            _, coff = parse_uleb128(data, coff)
        for _ in range(instance_fields_size):
            _, coff = parse_uleb128(data, coff)
            _, coff = parse_uleb128(data, coff)
            
        method_idx = 0
        for _ in range(direct_methods_size):
            method_idx_diff, coff = parse_uleb128(data, coff)
            method_idx += method_idx_diff
            access_flags, coff = parse_uleb128(data, coff)
            code_off, coff = parse_uleb128(data, coff)
            if code_off != 0:
                c_desc, m_name = get_method_name_and_class(method_idx)
                methods[f"{c_desc}->{m_name}"] = code_off
                
        method_idx = 0
        for _ in range(virtual_methods_size):
            method_idx_diff, coff = parse_uleb128(data, coff)
            method_idx += method_idx_diff
            access_flags, coff = parse_uleb128(data, coff)
            code_off, coff = parse_uleb128(data, coff)
            if code_off != 0:
                c_desc, m_name = get_method_name_and_class(method_idx)
                methods[f"{c_desc}->{m_name}"] = code_off
                
    return methods, data

def compare_all(dex1, dex2):
    m1, d1 = read_all_methods(dex1)
    m2, d2 = read_all_methods(dex2)
    
    diff_count = 0
    for name in m1:
        if name not in m2:
            print(f"Missing in {dex2}: {name}")
            continue
            
        off1 = m1[name]
        off2 = m2[name]
        
        # Read code items
        r1, i1, o1, t1, dbg1, isz1 = struct.unpack('<HHHHII', d1[off1:off1+16])
        r2, i2, o2, t2, dbg2, isz2 = struct.unpack('<HHHHII', d2[off2:off2+16])
        
        insns1 = d1[off1+16:off1+16+isz1*2]
        insns2 = d2[off2+16:off2+16+isz2*2]
        
        if insns1 != insns2:
            diff_count += 1
            print(f"Difference in {name}")
            w1 = [f"{w:04x}" for w in struct.unpack(f'<{isz1}H', insns1)]
            w2 = [f"{w:04x}" for w in struct.unpack(f'<{isz2}H', insns2)]
            print(f"  smali.jar: {' '.join(w1)}")
            print(f"  agent-ej  : {' '.join(w2)}")
            print()
            if diff_count > 10:
                print("Too many differences, stopping...")
                return
    print(f"Total differences found: {diff_count}")

compare_all('apk/work/classes_smali_jar.dex', 'apk/work/classes_test.dex')
