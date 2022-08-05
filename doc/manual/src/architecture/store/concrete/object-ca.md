# Content-Addressing Store Objects

Before in the section on [store paths](./paths.md), we talked abstractly about content-adressing store objects to preduce the digest part of their store paths.
Now we can make that precise.

Recall that store paths have the following form:

### Content Addressing

The contents of the store object are file system objects and references.
If one knows content addressing was used, one can recalculate the reference and thus verify the store object.

Content addressing is currently only used for the special cases of source files and "fixed-output derivations", where the contents of a store object are known in advance.
Content addressing of build results is still an [experimental feature subject to some restrictions](https://github.com/tweag/rfcs/blob/cas-rfc/rfcs/0062-content-addressed-paths.md).
