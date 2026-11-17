-- Getting started with MiniSQL
-- Run everything:          Ctrl+Shift+Enter (or the lightning button)
-- Run statement at cursor: Ctrl+Enter
-- Watch the right-hand panel: it explains which data structures each query used.

-- start clean so the script can be run again
DROP TABLE IF EXISTS enrollments;
DROP TABLE IF EXISTS courses;
DROP TABLE IF EXISTS students;

CREATE TABLE students (
  id     INT PRIMARY KEY,      -- primary key -> B+ tree
  name   VARCHAR(24),
  age    INT,
  city   VARCHAR(16),
  gpa    FLOAT,
  active BOOL
);

INSERT INTO students VALUES
  (1,  'Aarav',   19, 'Noida',     8.4, TRUE),
  (2,  'Diya',    20, 'Delhi',     9.1, TRUE),
  (3,  'Kabir',   21, 'Noida',     7.2, TRUE),
  (4,  'Meera',   20, 'Mumbai',    8.8, FALSE),
  (5,  'Rohan',   22, 'Delhi',     6.9, TRUE),
  (6,  'Ananya',  19, 'Pune',      9.4, TRUE),
  (7,  'Vihaan',  21, 'Noida',     7.8, TRUE),
  (8,  'Ishita',  20, 'Delhi',     8.1, TRUE),
  (9,  'Arjun',   23, 'Jaipur',    6.5, FALSE),
  (10, 'Saanvi',  19, 'Mumbai',    9.0, TRUE),
  (11, 'Kunal',   22, 'Pune',      7.5, TRUE),
  (12, 'Priya',   21, 'Noida',     8.6, TRUE),
  (13, 'Dev',     20, 'Jaipur',    7.0, TRUE),
  (14, 'Tara',    22, 'Delhi',     8.9, FALSE),
  (15, 'Nikhil',  19, 'Noida',     6.8, TRUE);

CREATE TABLE courses (
  code    VARCHAR(8) PRIMARY KEY,
  title   VARCHAR(32),
  credits INT
);

INSERT INTO courses VALUES
  ('DS101', 'Data Structures', 4),
  ('DB201', 'Database Systems', 4),
  ('OS202', 'Operating Systems', 3),
  ('AI301', 'Artificial Intelligence', 3),
  ('MA102', 'Discrete Mathematics', 3);

CREATE TABLE enrollments (
  id         INT PRIMARY KEY,
  student_id INT,
  course     VARCHAR(8),
  marks      INT
);

INSERT INTO enrollments VALUES
  (1, 1, 'DS101', 88), (2, 1, 'DB201', 76), (3, 2, 'DS101', 94),
  (4, 2, 'AI301', 91), (5, 3, 'OS202', 67), (6, 4, 'DS101', 85),
  (7, 5, 'MA102', 58), (8, 6, 'AI301', 97), (9, 6, 'DS101', 92),
  (10, 7, 'DB201', 73), (11, 8, 'OS202', 81), (12, 10, 'DS101', 90),
  (13, 11, 'MA102', 69), (14, 12, 'DB201', 84), (15, 14, 'AI301', 88);

-- 1. Point lookup on the primary key: B+ tree, root to leaf
SELECT * FROM students WHERE id = 7;

-- 2. Range on the primary key: find the first leaf, then follow leaf links
SELECT id, name, gpa FROM students WHERE id BETWEEN 4 AND 9;

-- 3. No index on age yet: full scan + WHERE evaluated on a stack
SELECT name, age FROM students WHERE age = 20;

-- 4. Add a secondary index (AVL tree) and run the same query again
CREATE INDEX idx_age ON students(age);
SELECT name, age FROM students WHERE age = 20;

-- 5. Join without an index on the join column: hash join
SELECT s.name, e.course, e.marks
FROM students s JOIN enrollments e ON s.id = e.student_id
WHERE e.marks > 80
ORDER BY marks DESC;

-- 6. Join where the other side is a primary key: index nested-loop join
SELECT e.student_id, c.title, c.credits
FROM enrollments e JOIN courses c ON e.course = c.code;

-- 7. GROUP BY uses a hash table from group key to running totals
SELECT city, COUNT(*) AS students, AVG(gpa) AS avg_gpa
FROM students GROUP BY city ORDER BY students DESC;

-- 8. ORDER BY on the primary key needs no sort (leaf chain is already ordered)
SELECT id, name FROM students ORDER BY id DESC LIMIT 5;

-- 9. Updates and deletes keep every index in sync
UPDATE students SET age = age + 1 WHERE city = 'Noida';
DELETE FROM enrollments WHERE marks < 60;

SHOW TABLES;
