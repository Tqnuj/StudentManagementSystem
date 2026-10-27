'use strict';

const state = { students: [], canWrite: false, ready: false, editId: null, error: null };
const $ = (id) => document.getElementById(id);
const tableBody = $('studentTableBody');
const modal = $('studentModal');
const form = $('studentForm');

function escapeHtml(value) {
  return String(value ?? '').replace(/[&<>"']/g, (ch) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[ch]);
}
function initials(name) {
  return String(name || '?').trim().split(/\s+/).slice(0, 2).map(part => part.charAt(0).toUpperCase()).join('') || '?';
}
function formatYear(year) {
  return ({ 1: '1st year', 2: '2nd year', 3: '3rd year', 4: '4th year' })[year] || `${year}th year`;
}
function showToast(title, message, isError = false) {
  const toast = document.createElement('div');
  toast.className = `toast${isError ? ' toast-error' : ''}`;
  const mark = document.createElement('span'); mark.className = 'toast-mark'; mark.textContent = isError ? '!' : '✓';
  const copy = document.createElement('div');
  const heading = document.createElement('strong'); heading.textContent = title;
  const body = document.createElement('p'); body.textContent = message;
  copy.append(heading, body); toast.append(mark, copy); $('toastRegion').append(toast);
  window.setTimeout(() => toast.remove(), 4200);
}
async function api(url, options = {}) {
  let response;
  try {
    response = await fetch(url, { ...options, headers: { ...(options.body ? { 'Content-Type': 'application/json' } : {}), ...(options.headers || {}) } });
  } catch (_) {
    throw new Error('Could not connect to the C++ server. Keep the terminal window open and refresh this page.');
  }
  let payload = {};
  try { payload = await response.json(); } catch (_) { /* error below is clearer */ }
  if (!response.ok) {
    const detail = payload.error || (payload.reasons || []).join(' ');
    throw new Error(detail || `Request failed (${response.status}).`);
  }
  return payload;
}
async function refreshStatus() {
  const status = await api('/api/status');
  state.canWrite = Boolean(status.canWrite);
  state.ready = Boolean(status.valid);
  state.error = (status.reasons || []).join(' ');
  $('storageStatus').innerHTML = state.canWrite
    ? '<span class="status-dot"></span> Storage ready'
    : '<span class="status-dot warn"></span> Read-only recovery mode';
  const banner = $('recoveryBanner');
  if (!state.canWrite) {
    banner.classList.remove('hidden');
    $('recoveryMessage').textContent = 'Student data could not pass the required checks. Normal changes are disabled to protect existing files.';
    const list = $('recoveryReasons'); list.replaceChildren();
    (status.reasons || ['Required student data is not ready.']).forEach(reason => {
      const item = document.createElement('li'); item.textContent = reason; list.append(item);
    });
  } else banner.classList.add('hidden');
  $('addStudentTop').disabled = !state.canWrite;
  $('addStudentTop').title = state.canWrite ? 'Add a new student' : 'Unavailable while recovery is required';
  return status;
}
async function loadStudents() {
  try {
    const status = await refreshStatus();
    if (!status.valid) {
      state.students = [];
      renderTable(); renderMetrics(); renderDepartments();
      tableBody.innerHTML = `<tr><td colspan="6" class="table-state"><span class="empty-icon">⌑</span><div class="empty-title">Student records are unavailable</div><div class="empty-copy">The data file must be checked before records can be displayed. Open the recovery guide for safe next steps.</div></td></tr>`;
      $('resultCount').textContent = 'Student data requires attention';
      return;
    }
    state.students = await api('/api/students');
    renderMetrics(); renderDepartments(); renderTable();
  } catch (error) {
    state.students = []; state.canWrite = false; state.ready = false; $('addStudentTop').disabled = true;
    tableBody.innerHTML = `<tr><td colspan="6" class="table-state"><span class="empty-icon">!</span><div class="empty-title">Could not load student records</div><div class="empty-copy">${escapeHtml(error.message)}</div></td></tr>`;
    showToast('Connection problem', error.message, true);
    $('storageStatus').innerHTML = '<span class="status-dot warn"></span> Connection issue';
  }
}
function filteredStudents() {
  const query = $('searchInput').value.trim().toLowerCase();
  const department = $('departmentFilter').value;
  const status = $('statusFilter').value;
  return state.students.filter(student => {
    const haystack = `${student.name} ${student.roll} ${student.department} ${student.email}`.toLowerCase();
    return (!query || haystack.includes(query)) && (department === 'all' || student.department === department) && (status === 'all' || student.status === status);
  }).sort((a, b) => a.name.localeCompare(b.name));
}
function renderTable() {
  const students = filteredStudents();
  if (!students.length) {
    const hasFilters = $('searchInput').value.trim() || $('departmentFilter').value !== 'all' || $('statusFilter').value !== 'all';
    tableBody.innerHTML = `<tr><td colspan="6" class="table-state"><span class="empty-icon">⌕</span><div class="empty-title">${hasFilters ? 'No matching students' : 'No student records yet'}</div><div class="empty-copy">${hasFilters ? 'Try another search or change your filters.' : 'Add your first student to start building the directory.'}</div></td></tr>`;
    $('resultCount').textContent = hasFilters ? 'No matching records' : 'No records yet';
    return;
  }
  tableBody.innerHTML = students.map((student, index) => {
    const avatarClass = `avatar-${index % 5}`;
    const gpaWidth = Math.max(0, Math.min(100, Number(student.gpa) * 10));
    return `<tr>
      <td><div class="student-cell"><div class="student-avatar ${avatarClass}">${escapeHtml(initials(student.name))}</div><div><div class="student-name" title="${escapeHtml(student.name)}">${escapeHtml(student.name)}</div><div class="student-roll">${escapeHtml(student.roll)}</div></div></div></td>
      <td class="dept-cell">${escapeHtml(student.department)}</td>
      <td><span class="year-pill">${escapeHtml(formatYear(Number(student.year)))}</span></td>
      <td class="gpa-cell"><span class="gpa-number">${Number(student.gpa).toFixed(2)}</span><div class="gpa-track"><span class="gpa-fill" style="width:${gpaWidth}%"></span></div></td>
      <td><span class="status-pill ${student.status === 'Active' ? 'status-active' : 'status-inactive'}">${escapeHtml(student.status)}</span></td>
      <td><div class="row-actions"><button class="icon-button" type="button" data-action="edit" data-id="${student.id}" aria-label="Edit ${escapeHtml(student.name)}" title="Edit student" ${state.canWrite ? '' : 'disabled'}>✎</button><button class="icon-button delete" type="button" data-action="delete" data-id="${student.id}" aria-label="Delete ${escapeHtml(student.name)}" title="Delete student" ${state.canWrite ? '' : 'disabled'}>⌫</button></div></td>
    </tr>`;
  }).join('');
  $('resultCount').textContent = `Showing ${students.length} of ${state.students.length} ${state.students.length === 1 ? 'record' : 'records'}`;
}
function renderMetrics() {
  const students = state.students;
  const active = students.filter(s => s.status === 'Active').length;
  const departments = new Set(students.map(s => s.department));
  const average = students.length ? students.reduce((sum, s) => sum + Number(s.gpa), 0) / students.length : 0;
  $('totalStudents').textContent = students.length;
  $('activeStudents').textContent = active;
  $('averageGpa').innerHTML = `${students.length ? average.toFixed(2) : '—'}<small> / 10</small>`;
  $('departmentCount').textContent = departments.size;
  $('navStudentCount').textContent = students.length;
  $('totalStudentsFoot').textContent = students.length === 1 ? '1 record in your workspace' : 'Records in your workspace';
  $('activeStudentsFoot').textContent = `${students.length ? Math.round(active / students.length * 100) : 0}% of all student records`;
  $('chartTotal').textContent = `${students.length} ${students.length === 1 ? 'student' : 'students'}`;
}
function renderDepartments() {
  const counts = new Map();
  state.students.forEach(s => counts.set(s.department, (counts.get(s.department) || 0) + 1));
  const departments = [...counts.entries()].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]));
  const filter = $('departmentFilter'); const old = filter.value;
  filter.innerHTML = '<option value="all">All departments</option>' + departments.map(([name]) => `<option value="${escapeHtml(name)}">${escapeHtml(name)}</option>`).join('');
  filter.value = departments.some(([name]) => name === old) ? old : 'all';
  if (!departments.length) { $('departmentChart').innerHTML = '<div class="chart-empty">Your department summary will appear here.</div>'; return; }
  const max = departments[0][1];
  $('departmentChart').innerHTML = departments.slice(0, 5).map(([name, count], i) => `<div class="chart-row"><div class="chart-row-top"><span class="chart-name">${escapeHtml(name)}</span><span class="chart-count">${count} ${count === 1 ? 'student' : 'students'}</span></div><div class="chart-track"><div class="chart-bar bar-${i % 4}" style="width:${Math.max(4, count / max * 100)}%"></div></div></div>`).join('');
}
function openModal(student = null) {
  if (!state.canWrite) { showToast('Read-only mode', 'Student changes are disabled until the required data validation or recovery is complete.', true); return; }
  state.editId = student ? student.id : null;
  form.reset(); $('formError').classList.add('hidden'); $('studentId').value = student ? student.id : '';
  $('modalTitle').textContent = student ? 'Edit student' : 'Add a student';
  $('modalKicker').textContent = student ? 'UPDATE RECORD' : 'NEW RECORD';
  $('saveStudentButton').innerHTML = student ? 'Save changes <span>→</span>' : 'Save student <span>→</span>';
  if (student) {
    $('studentName').value = student.name; $('studentRoll').value = student.roll;
    if (![...$('studentDepartment').options].some(o => o.value === student.department)) {
      const option = document.createElement('option'); option.value = student.department; option.textContent = student.department; $('studentDepartment').append(option);
    }
    $('studentDepartment').value = student.department;
    $('studentYear').value = String(student.year); $('studentGpa').value = Number(student.gpa).toFixed(2);
    $('studentEmail').value = student.email; $('studentPhone').value = student.phone; $('studentStatus').value = student.status;
  }
  modal.classList.remove('hidden'); document.body.style.overflow = 'hidden'; window.setTimeout(() => $('studentName').focus(), 60);
}
function closeModal() { modal.classList.add('hidden'); document.body.style.overflow = ''; state.editId = null; }
function getStudentFormData() {
  return {
    name: $('studentName').value.trim(), roll: $('studentRoll').value.trim(), department: $('studentDepartment').value,
    year: Number($('studentYear').value), gpa: Number($('studentGpa').value), email: $('studentEmail').value.trim(),
    phone: $('studentPhone').value.trim(), status: $('studentStatus').value
  };
}
async function saveStudent(event) {
  event.preventDefault();
  const errorBox = $('formError'); errorBox.classList.add('hidden');
  if (!form.reportValidity()) return;
  const payload = getStudentFormData();
  const button = $('saveStudentButton'); button.disabled = true; button.textContent = 'Saving…';
  try {
    const editing = state.editId !== null;
    await api(editing ? `/api/students/${state.editId}` : '/api/students', { method: editing ? 'PUT' : 'POST', body: JSON.stringify(payload) });
    closeModal(); await loadStudents(); showToast(editing ? 'Student updated' : 'Student added', editing ? 'The student record was saved successfully.' : 'The new student is now in your directory.');
  } catch (error) {
    errorBox.textContent = error.message; errorBox.classList.remove('hidden');
  } finally {
    button.disabled = false; button.innerHTML = state.editId !== null ? 'Save changes <span>→</span>' : 'Save student <span>→</span>';
  }
}
async function deleteStudent(id) {
  if (!state.canWrite) return;
  const student = state.students.find(s => s.id === id); if (!student) return;
  if (!window.confirm(`Delete ${student.name} (${student.roll})? This change will be saved to students.csv.`)) return;
  try { await api(`/api/students/${id}`, { method: 'DELETE' }); await loadStudents(); showToast('Student deleted', `${student.name}'s record was removed.`); }
  catch (error) { showToast('Could not delete student', error.message, true); }
}
function exportCsv() {
  if (!state.ready) { showToast('Export unavailable', 'The student data must pass validation before it can be exported.', true); return; }
  const rows = [['id','name','roll','department','year','email','phone','gpa','status'], ...state.students.map(s => [s.id,s.name,s.roll,s.department,s.year,s.email,s.phone,Number(s.gpa).toFixed(2),s.status])];
  const csv = rows.map(row => row.map(cell => { const value = String(cell ?? ''); return /[",\r\n]/.test(value) ? `"${value.replace(/"/g, '""')}"` : value; }).join(',')).join('\r\n');
  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' }); const url = URL.createObjectURL(blob);
  const a = document.createElement('a'); a.href = url; a.download = 'student-records-export.csv'; document.body.append(a); a.click(); a.remove(); URL.revokeObjectURL(url);
  showToast('CSV exported', 'A copy of your student records has been downloaded.');
}

$('addStudentTop').addEventListener('click', () => openModal());
$('closeModal').addEventListener('click', closeModal); $('cancelModal').addEventListener('click', closeModal);
modal.addEventListener('click', event => { if (event.target === modal) closeModal(); });
form.addEventListener('submit', saveStudent);
tableBody.addEventListener('click', event => {
  const button = event.target.closest('button[data-action]'); if (!button || !state.canWrite) return;
  const id = Number(button.dataset.id);
  if (button.dataset.action === 'edit') { const student = state.students.find(s => s.id === id); if (student) openModal(student); }
  if (button.dataset.action === 'delete') deleteStudent(id);
});
$('searchInput').addEventListener('input', renderTable);
$('departmentFilter').addEventListener('change', renderTable); $('statusFilter').addEventListener('change', renderTable);
$('exportCsvButton').addEventListener('click', exportCsv);
document.addEventListener('keydown', event => {
  if (event.key === 'Escape' && !modal.classList.contains('hidden')) closeModal();
  if (event.key === '/' && !['INPUT','TEXTAREA','SELECT'].includes(document.activeElement.tagName) && modal.classList.contains('hidden')) { event.preventDefault(); $('searchInput').focus(); }
});
$('currentDate').textContent = new Intl.DateTimeFormat(undefined, { weekday: 'short', month: 'short', day: 'numeric', year: 'numeric' }).format(new Date());
loadStudents();
