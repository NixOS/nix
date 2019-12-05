use super::{
    error,
    foreign::{self, CBox},
    store::StorePath,
    util,
};

#[no_mangle]
pub extern "C" fn unpack_tarfile(
    source: foreign::Source,
    dest_dir: &str,
) -> CBox<Result<(), error::CppException>> {
    CBox::new(util::tarfile::unpack_tarfile(source, dest_dir).map_err(|err| err.into()))
}

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
    StorePath::new(std::path::Path::new(path), store_dir).map_err(|err| err.into())
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
pub extern "C" fn ffi_StorePath_to_string(self_: &StorePath) -> String {
    format!("{}", self_)
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
