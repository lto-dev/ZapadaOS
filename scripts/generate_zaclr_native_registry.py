import pathlib
import re
import sys


def decode_wrapper_type_name(wrapper_name: str):
    encoded = wrapper_name[len('zaclr_native_'):]
    decoded = encoded.replace('__', '\0').replace('_', '.').replace('\0', '_')
    index = decoded.rfind('.')
    return ('', decoded) if index < 0 else (decoded[:index], decoded[index + 1:])


def parse_named_type(text: str, index: int):
    end = index
    while end < len(text) and (text[end].isalnum() or text[end] == '_'):
        end += 1
    decoded = text[index:end].replace('__', '\0').replace('_', '.').replace('\0', '_')
    split = decoded.rfind('.')
    namespace = '' if split < 0 else decoded[:split]
    name = decoded if split < 0 else decoded[split + 1:]
    return namespace, name, end


def parse_sig_type(text: str, primitive_map: dict[str, str], index: int = 0):
    flags: list[str] = []
    while True:
        if text.startswith('BYREF_', index):
            flags.append('ZACLR_NATIVE_BIND_SIG_FLAG_BYREF')
            index += len('BYREF_')
            continue
        if text.startswith('BYREF', index) and index + len('BYREF') == len(text):
            flags.append('ZACLR_NATIVE_BIND_SIG_FLAG_BYREF')
            index += len('BYREF')
            continue
        if text.startswith('PINNED_', index):
            flags.append('ZACLR_NATIVE_BIND_SIG_FLAG_PINNED')
            index += len('PINNED_')
            continue
        if text.startswith('PINNED', index) and index + len('PINNED') == len(text):
            flags.append('ZACLR_NATIVE_BIND_SIG_FLAG_PINNED')
            index += len('PINNED')
            continue
        break

    if index == len(text):
        return ({'kind': 'VOID', 'flags': flags, 'opaque': True}, index)

    for prefix, kind in [('SZARRAY_', 'SZARRAY'), ('PTR_', 'PTR')]:
        if text.startswith(prefix, index):
            child, index = parse_sig_type(text, primitive_map, index + len(prefix))
            return ({'kind': kind, 'flags': flags, 'child': child}, index)

    for prefix, kind in [('VAR_', 'VAR'), ('MVAR_', 'MVAR')]:
        if text.startswith(prefix, index):
            match = re.match(r'\d+', text[index + len(prefix):])
            if not match:
                raise RuntimeError(f'malformed wrapper declaration parse: {text}')
            return ({'kind': kind, 'flags': flags, 'generic_index': int(match.group(0))}, index + len(prefix) + len(match.group(0)))

    if text.startswith('GENERICINST_', index):
        index += len('GENERICINST_')
        if text.startswith('CLASS_', index):
            owner_kind = 'CLASS'
            index += len('CLASS_')
        elif text.startswith('VALUETYPE_', index):
            owner_kind = 'VALUETYPE'
            index += len('VALUETYPE_')
        else:
            raise RuntimeError(f'unsupported signature vocabulary: {text}')
        namespace, name, index = parse_named_type(text, index)
        if not text.startswith('__', index):
            raise RuntimeError(f'malformed wrapper declaration parse: {text}')
        index += 2
        match = re.match(r'\d+', text[index:])
        if not match:
            raise RuntimeError(f'malformed wrapper declaration parse: {text}')
        count = int(match.group(0))
        index += len(match.group(0))
        args = []
        for _ in range(count):
            if not text.startswith('__', index):
                raise RuntimeError(f'malformed wrapper declaration parse: {text}')
            arg, index = parse_sig_type(text, primitive_map, index + 2)
            args.append(arg)
        return ({'kind': 'GENERICINST', 'flags': flags, 'type_namespace': namespace, 'type_name': name, 'generic_args': args, 'owner_kind': owner_kind}, index)

    for token, kind in [('CLASS', 'CLASS'), ('VALUETYPE', 'VALUETYPE')]:
        if text.startswith(token, index):
            next_index = index + len(token)
            if next_index == len(text):
                return ({'kind': kind, 'flags': flags, 'type_namespace': '', 'type_name': None}, next_index)
            if text.startswith('_', next_index):
                namespace, name, index = parse_named_type(text, next_index + 1)
                return ({'kind': kind, 'flags': flags, 'type_namespace': namespace, 'type_name': name}, index)

    for key in sorted(primitive_map.keys(), key=len, reverse=True):
        if text.startswith(key, index):
            return ({'kind': key, 'flags': flags}, index + len(key))

    raise RuntimeError(f'unsupported signature vocabulary: {text[index:]}')


def emit_sig_type(spec: dict, name_hint: str, primitive_map: dict[str, str]):
    emitted: list[str] = []
    child_name = 'nullptr'
    generic_name = 'nullptr'
    generic_count = 0
    if spec['kind'] in ('SZARRAY', 'PTR'):
        child_symbol, child_defs = emit_sig_type(spec['child'], name_hint + '_child', primitive_map)
        emitted.extend(child_defs)
        child_name = '&' + child_symbol
    if spec['kind'] == 'GENERICINST':
        arg_names = []
        for index, arg in enumerate(spec['generic_args']):
            arg_symbol, arg_defs = emit_sig_type(arg, f'{name_hint}_arg{index}', primitive_map)
            emitted.extend(arg_defs)
            arg_names.append(arg_symbol)
        args_array = f'{name_hint}_generic_args'
        emitted.append('    static const struct zaclr_native_bind_sig_type ' + args_array + '[] = {\n' + ',\n'.join('        ' + name for name in arg_names) + '\n    };')
        generic_name = args_array
        generic_count = len(arg_names)
    flag_expr = ' | '.join(spec['flags']) if spec['flags'] else 'ZACLR_NATIVE_BIND_SIG_FLAG_NONE'
    type_namespace = 'nullptr'
    type_name = 'nullptr'
    generic_index = spec.get('generic_index', 0)
    element_type = primitive_map.get(spec['kind'])
    if spec['kind'] == 'CLASS':
        element_type = 'ZACLR_ELEMENT_TYPE_CLASS'
        type_namespace = f'"{spec["type_namespace"]}"' if spec['type_namespace'] else 'nullptr'
        type_name = f'"{spec["type_name"]}"' if spec.get('type_name') else 'nullptr'
    elif spec['kind'] == 'VALUETYPE':
        element_type = 'ZACLR_ELEMENT_TYPE_VALUETYPE'
        type_namespace = f'"{spec["type_namespace"]}"' if spec['type_namespace'] else 'nullptr'
        type_name = f'"{spec["type_name"]}"' if spec.get('type_name') else 'nullptr'
    elif spec['kind'] == 'SZARRAY':
        element_type = 'ZACLR_ELEMENT_TYPE_SZARRAY'
    elif spec['kind'] == 'PTR':
        element_type = 'ZACLR_ELEMENT_TYPE_PTR'
    elif spec['kind'] == 'VAR':
        element_type = 'ZACLR_ELEMENT_TYPE_VAR'
    elif spec['kind'] == 'MVAR':
        element_type = 'ZACLR_ELEMENT_TYPE_MVAR'
    elif spec['kind'] == 'GENERICINST':
        element_type = 'ZACLR_ELEMENT_TYPE_GENERICINST'
        type_namespace = f'"{spec["type_namespace"]}"' if spec['type_namespace'] else 'nullptr'
        type_name = f'"{spec["type_name"]}"'
    if element_type is None:
        raise RuntimeError(f'unsupported signature vocabulary: {spec}')
    current_name = f'{name_hint}_type'
    emitted.append(f'    static const struct zaclr_native_bind_sig_type {current_name} = {{ {element_type}, {flag_expr}, {generic_index}, {type_namespace}, {type_name}, {child_name}, {generic_name}, {generic_count}, 0u }};')
    return current_name, emitted


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit('usage: generate_zaclr_native_registry.py <library_name> <library_dir> <output_path>')

    library_name = sys.argv[1]
    library_dir = pathlib.Path(sys.argv[2])
    output_path = pathlib.Path(sys.argv[3])
    symbol = re.sub(r'[^A-Za-z0-9]', '_', library_name)
    headers = sorted(library_dir.glob('zaclr_native_*.h'))

    primitive_map = {
        'VOID': 'ZACLR_ELEMENT_TYPE_VOID',
        'BOOLEAN': 'ZACLR_ELEMENT_TYPE_BOOLEAN',
        'CHAR': 'ZACLR_ELEMENT_TYPE_CHAR',
        'I1': 'ZACLR_ELEMENT_TYPE_I1',
        'U1': 'ZACLR_ELEMENT_TYPE_U1',
        'I2': 'ZACLR_ELEMENT_TYPE_I2',
        'U2': 'ZACLR_ELEMENT_TYPE_U2',
        'I4': 'ZACLR_ELEMENT_TYPE_I4',
        'U4': 'ZACLR_ELEMENT_TYPE_U4',
        'I8': 'ZACLR_ELEMENT_TYPE_I8',
        'U8': 'ZACLR_ELEMENT_TYPE_U8',
        'R4': 'ZACLR_ELEMENT_TYPE_R4',
        'R8': 'ZACLR_ELEMENT_TYPE_R8',
        'STRING': 'ZACLR_ELEMENT_TYPE_STRING',
        'OBJECT': 'ZACLR_ELEMENT_TYPE_OBJECT',
        'I': 'ZACLR_ELEMENT_TYPE_I',
        'U': 'ZACLR_ELEMENT_TYPE_U'
    }

    rows: list[str] = []
    defs: list[str] = []
    seen: set[tuple[str, str, str, str]] = set()

    for header in headers:
        text = header.read_text()
        wrapper_match = re.search(r'struct\s+(zaclr_native_[A-Za-z0-9_]+)', text)
        if not wrapper_match:
            continue
        wrapper_name = wrapper_match.group(1)
        type_namespace, type_name = decode_wrapper_type_name(wrapper_name)
        for method_match in re.finditer(r'static\s+struct\s+zaclr_result\s+([A-Za-z0-9_]+)\s*\(', text):
            method_name = method_match.group(1)
            managed_name, signature_suffix = method_name.split('___', 1)
            if managed_name == '_ctor':
                managed_name = '.ctor'
            key = (type_namespace, type_name, managed_name, signature_suffix)
            if key in seen:
                raise RuntimeError(f'duplicate exact bind entries in one assembly: {key}')
            seen.add(key)

            signature_parts = signature_suffix.split('__')
            has_this = 1
            if signature_parts and signature_parts[0] == 'STATIC':
                has_this = 0
                signature_parts = signature_parts[1:]
            elif signature_parts and signature_parts[0] == 'INSTANCE':
                has_this = 1
                signature_parts = signature_parts[1:]
            return_spec, consumed = parse_sig_type(signature_parts[0], primitive_map, 0)
            if consumed != len(signature_parts[0]):
                raise RuntimeError(f'mismatch between generated bind type model and wrapper suffix shape: {method_name}')
            param_specs = []
            for part in signature_parts[1:]:
                param_spec, consumed = parse_sig_type(part, primitive_map, 0)
                if consumed != len(part):
                    raise RuntimeError(f'mismatch between generated bind type model and wrapper suffix shape: {method_name}')
                param_specs.append(param_spec)

            ret_name, ret_defs = emit_sig_type(return_spec, f'method_{len(rows)}_ret', primitive_map)
            defs.extend(ret_defs)
            parameter_expr = 'nullptr'
            if param_specs:
                param_names = []
                for index, spec in enumerate(param_specs):
                    param_name, param_defs = emit_sig_type(spec, f'method_{len(rows)}_param{index}', primitive_map)
                    defs.extend(param_defs)
                    param_names.append(param_name)
                array_name = f'method_{len(rows)}_params'
                defs.append('    static const struct zaclr_native_bind_sig_type ' + array_name + '[] = {\n' + ',\n'.join('        ' + name for name in param_names) + '\n    };')
                parameter_expr = array_name
            has_this_value = '0u' if has_this == 0 else '1u'
            rows.append(f'        {{ "{type_namespace}", "{type_name}", "{managed_name}", {{ {has_this_value}, {len(param_specs)}, 0u, {ret_name}, {parameter_expr} }}, &{wrapper_name}::{method_name} }}')

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open('w', encoding='utf-8') as stream:
        stream.write('/* Auto-generated ZACLR native lookup table. */\n')
        stream.write('/* Generated from assembly-local ZACLR wrapper declarations. */\n\n')
        stream.write(f'#include <{library_name}/zaclr_native_registry.h>\n\n')
        stream.write('namespace {\n')
        for definition in defs:
            stream.write(definition + '\n')
        stream.write('\n')
        stream.write('    static const struct zaclr_native_bind_method method_lookup[] = {\n')
        stream.write(',\n'.join(rows))
        stream.write('\n    };\n\n')
        stream.write('    static const struct zaclr_native_assembly_descriptor s_descriptor = {\n')
        stream.write(f'        "{library_name}",\n')
        stream.write('        method_lookup,\n')
        stream.write('        (uint32_t)(sizeof(method_lookup) / sizeof(method_lookup[0]))\n')
        stream.write('    };\n')
        stream.write('}\n\n')
        stream.write(f'extern "C" const struct zaclr_native_assembly_descriptor* zaclr_native_{symbol}_descriptor(void)\n')
        stream.write('{\n')
        stream.write('    return &s_descriptor;\n')
        stream.write('}\n')

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
