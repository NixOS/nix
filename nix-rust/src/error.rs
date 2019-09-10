#[derive(Debug)]
pub enum Error {
    Misc(String),
    Foreign(libc::c_void), // == std::exception_ptr
}
