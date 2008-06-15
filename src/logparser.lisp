#| Installation
(progn
  (require 'asdf)
  (require 'asdf-install)
  (asdf-install:install :cl-fad)
  (asdf-install:install :cl-ppcre))
|#

(require 'cl-ppcre)
(require 'cl-fad)
(use-package :cl-ppcre)

(defvar *memory-mappings* (make-hash-table :test 'equal))
(defvar *memory-mappings-counter* 0)
(defvar *alloc-sequence* nil)

(defun next-memory-mapping-counter ()
  (incf *memory-mappings-counter*))

(defun pointer->id (pointer)
  "Return the counter corresponding to the pointer. Adds the pointer to the mappings table and assigns a counter if it doesn't already exist."
  (multiple-value-bind (id presentp) (gethash pointer *memory-mappings*)
   (if presentp
       id
       (setf (gethash pointer *memory-mappings*) (next-memory-mapping-counter)))))

(defun replace-pointer (old new)
  "Replace pointer old with new in the mapping, but keep the counter."
  (multiple-value-bind (counter presentp) (gethash old *memory-mappings*)
    (when presentp
      (remhash old *memory-mappings*))
    (setf (gethash new *memory-mappings*) counter)))

(defun parse-logs-and-generate-alloc-sequence (path)
  "Output a list of allocations, where the pointers have been remapped to 
  a monotonically increasing integer (used to name variables, or possibly index into a list of pointers)"
  (with-open-file (s path)
    (clrhash *memory-mappings*)
    (setf *memory-mappings-counter* 0)
    (setf *alloc-sequence* nil)
    (do ((line (read-line s) (read-line s nil 'eof)))
        ((eq line 'eof) nil)
        (multiple-value-bind (match regs) (scan-to-strings "MEM: 0[Xx]\\w+ malloc\\((\\d+)\\) => (0[Xx]\\w+)" line)
                             (declare (ignore match))
                             (when regs
                                   ;; add to known pointers list
                                   (let ((id (pointer->id (aref regs 1))))
                                        ;; output a malloc request
                                        (push `(malloc :size ,(read-from-string (aref regs 0)) :id ,id)
                                              *alloc-sequence*))))
        (multiple-value-bind (match regs) (scan-to-strings "MEM: 0[Xx]\\w+ realloc\\((\\w+),(\\d+)\\) => (0[Xx]\\w+)" line)
                             (declare (ignore match))
                             (when regs
                                   (let ((old-id (pointer->id (aref regs 0))))
                                      (replace-pointer (aref regs 0) (aref regs 2))
                                      (push `(realloc :id ,old-id
                                                      :size ,(read-from-string (aref regs 1))
                                                      :new-id ,(pointer->id (aref regs 2)))
                                            *alloc-sequence*))))

                                   ;; insert a FREE op
                                   ;;(push `(free :id ,(pointer->id (aref regs 0)))
                                   ;;      *alloc-sequence*)
                                   ;;(let ((id (pointer->id (aref regs 2))))
                                   ;;     ;; insert a MALLOC op
                                   ;;     (push `(malloc :size ,(read-from-string (aref regs 1)) :id ,id)
                                   ;;           *alloc-sequence*)))
                                   
        (multiple-value-bind (match regs) (scan-to-strings "MEM: 0[Xx]\\w+ free\\((\\w+)\\)" line)
                             (declare (ignore match))
                             (when regs
                                   (push `(free :id ,(pointer->id (aref regs 0)))
                                         *alloc-sequence*))))
    (setf *alloc-sequence* (nreverse *alloc-sequence*))
    nil))

(defun memory-op-to-statement (op)
  (case (first op)
    (malloc (format nil "void *mem_~A = rmalloc(~A);"
                    (getf (rest op) :id) (getf (rest op) :size)))
    (free (format nil "rmfree(mem_~A);" (getf (rest op) :id)))
    (realloc (format nil "mem_~A = rmrealloc(~A, mem_~A);"
                    (getf (rest op) :id)
                    (getf (rest op) :size)
                    (getf (rest op) :new-id)))
    (otherwise "")))

(defun flatten-to-c-file (path)
  (with-open-file (s path :direction :output
                          :if-does-not-exist :create
                          :if-exists :overwrite)
    (format s "
int alloc_test() {
~{    ~A~^~%~}
}" (remove-if (lambda (line) (zerop (length line))) (mapcar #'memory-op-to-statement *alloc-sequence*)))))

;;;;;;;;;;;;;;

(parse-logs-and-generate-alloc-sequence #P"aftonbladet/start-to-aftonbladet.log")
(flatten-to-c-file "logparsed.c")

