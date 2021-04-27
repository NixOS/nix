# A Simple Nix Expression

This section shows how to add and test the [GNU Hello
package](http://www.gnu.org/software/hello/hello.html) to the Nix
Packages collection. Hello is a program that prints out the text “Hello,
world\!”.

To add a package to the Nix Packages collection, you generally need to
do three things:

1.  Write a Nix expression for the package. This is a file that
    describes all the inputs involved in building the package, such as
    dependencies, sources, and so on.

2.  Write a *builder*. This is a shell script that builds the package
    from the inputs. (In fact, it can be written in any language, but
    typically it's a `bash` shell script.)

3.  Add the package to the file `pkgs/top-level/all-packages.nix`. The
    Nix expression written in the first step is a *function*; it
    requires other packages in order to build it. In this step you put
    it all together, i.e., you call the function with the right
    arguments to build the actual package.
