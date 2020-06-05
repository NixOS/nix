use super::{error, store::path, store::StorePath, util};

#[no_mangle]
pub unsafe extern "C" fn ffi_String_new(s: &str, out: *mut String) {
    // FIXME: check whether 's' is valid UTF-8?
    out.write(s.to_string())
}

#[no_mangle]
pub unsafe extern "C" fn ffi_String_drop(self_: *mut String) {
    std::ptr::drop_in_place(self_);
}

#[no_mangle]
pub extern "C" fn ffi_StorePath_new(
    path: &str,
    store_dir: &str,
) -> Result<StorePath, error::CppException> {
    StorePath::new(std::path::Path::new(path), std::path::Path::new(store_dir))
        .map_err(|err| err.into())
}

#[no_mangle]
pub extern "C" fn ffi_StorePath_new2(
    hash: &[u8; crate::store::path::STORE_PATH_HASH_BYTES],
    name: &str,
) -> Result<StorePath, error::CppException> {
    StorePath::from_parts(*hash, name).map_err(|err| err.into())
}

#[no_mangle]
pub extern "C" fn ffi_StorePath_fromBaseName(
    base_name: &str,
) -> Result<StorePath, error::CppException> {
    StorePath::new_from_base_name(base_name).map_err(|err| err.into())
}

#[no_mangle]
pub unsafe extern "C" fn ffi_StorePath_drop(self_: *mut StorePath) {
    std::ptr::drop_in_place(self_);
}

#[no_mangle]
pub extern "C" fn ffi_StorePath_to_string(self_: &StorePath) -> Vec<u8> {
    let mut buf = vec![0; path::STORE_PATH_HASH_CHARS + 1 + self_.name.name().len()];
    util::base32::encode_into(self_.hash.hash(), &mut buf[0..path::STORE_PATH_HASH_CHARS]);
    buf[path::STORE_PATH_HASH_CHARS] = b'-';
    buf[path::STORE_PATH_HASH_CHARS + 1..].clone_from_slice(self_.name.name().as_bytes());
    buf
}

#[no_mangle]
pub extern "C" fn ffi_StorePath_less_than(a: &StorePath, b: &StorePath) -> bool {
    a < b
}

#[no_mangle]
pub extern "C" fn ffi_StorePath_eq(a: &StorePath, b: &StorePath) -> bool {
    a == b
}

#[no_mangle]
pub extern "C" fn ffi_StorePath_clone(self_: &StorePath) -> StorePath {
    self_.clone()
}

#[no_mangle]
pub extern "C" fn ffi_StorePath_name(self_: &StorePath) -> &str {
    self_.name.name()
}

#[no_mangle]
pub extern "C" fn ffi_StorePath_hash_data(
    self_: &StorePath,
) -> &[u8; crate::store::path::STORE_PATH_HASH_BYTES] {
    self_.hash.hash()
}
