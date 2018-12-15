# SQLite bindings

There are some sqlite3 bindings in the directory natives/sqlite3 bundled with
the janet source code. They serve mostly as a
proof of concept external c library. To use, first compile the module with Make.

```sh
make natives
```

Next, enter the repl and create a database and a table.

```
janet:1:> (import natives/sqlite3 :as sql)
nil
janet:2:> (def db (sql/open "test.db"))
<sqlite3.connection 0x5561A138C470>
janet:3:> (sql/eval db `CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT);`)
@[]
janet:4:> (sql/eval db `INSERT INTO customers VALUES(:id, :name);` {:name "John" :id 12345})
@[]
janet:5:> (sql/eval db `SELECT * FROM customers;`)
@[{"id" 12345 "name" "John"}]
```

Finally, close the database connection when done with it.

```
janet:6:> (sql/close db)
nil
```
